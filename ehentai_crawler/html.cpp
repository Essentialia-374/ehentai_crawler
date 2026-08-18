#include "html.h"

#include <cctype>
#include <cstring>
#include <utility>

namespace html {

namespace {

const GumboVector* childrenOf(const GumboNode* node) {
    if (!node) {
        return nullptr;
    }
    if (node->type == GUMBO_NODE_DOCUMENT) {
        return &node->v.document.children;
    }
    if (node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE) {
        return &node->v.element.children;
    }
    return nullptr;
}

void walk(const GumboNode* node, const Predicate& match, std::vector<const GumboNode*>& out, bool stopAtFirst) {
    if (!node) {
        return;
    }
    if (isElement(node) && match(node)) {
        out.push_back(node);
        if (stopAtFirst) {
            return;
        }
    }
    const GumboVector* kids = childrenOf(node);
    if (!kids) {
        return;
    }
    for (unsigned int i = 0; i < kids->length; ++i) {
        walk(static_cast<const GumboNode*>(kids->data[i]), match, out, stopAtFirst);
        if (stopAtFirst && !out.empty()) {
            return;
        }
    }
}

void collectText(const GumboNode* node, std::string& out) {
    if (!node) {
        return;
    }
    if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_CDATA) {
        out.append(node->v.text.text);
        return;
    }
    if (node->type == GUMBO_NODE_WHITESPACE) {
        out.push_back(' ');
        return;
    }
    if (isElement(node)) {
        GumboTag tag = node->v.element.tag;
        if (tag == GUMBO_TAG_SCRIPT || tag == GUMBO_TAG_STYLE) {
            return;
        }
        if (tag == GUMBO_TAG_BR) {
            out.push_back(' ');
        }
    }
    const GumboVector* kids = childrenOf(node);
    if (!kids) {
        return;
    }
    for (unsigned int i = 0; i < kids->length; ++i) {
        collectText(static_cast<const GumboNode*>(kids->data[i]), out);
    }
}

bool isSpace(unsigned char c) {
    return std::isspace(c) != 0;
}

// Collapses whitespace runs to a single space and trims both ends. Non breaking
// spaces are common in the gallery tables, so they are folded in as well.
std::string normalise(const std::string& raw) {
    std::string collapsed;
    collapsed.reserve(raw.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        bool nbsp = c == 0xC2 && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xA0;
        if (nbsp) {
            ++i;
        }
        if (nbsp || isSpace(c)) {
            pendingSpace = !collapsed.empty();
            continue;
        }
        if (pendingSpace) {
            collapsed.push_back(' ');
            pendingSpace = false;
        }
        collapsed.push_back(static_cast<char>(c));
    }
    return collapsed;
}

}

Document::Document(const std::string& source)
    : output_(gumbo_parse_with_options(&kGumboDefaultOptions, source.c_str(), source.size())) {
}

Document::~Document() {
    if (output_) {
        gumbo_destroy_output(&kGumboDefaultOptions, output_);
    }
}

Document::Document(Document&& other) noexcept : output_(other.output_) {
    other.output_ = nullptr;
}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        if (output_) {
            gumbo_destroy_output(&kGumboDefaultOptions, output_);
        }
        output_ = other.output_;
        other.output_ = nullptr;
    }
    return *this;
}

bool isElement(const GumboNode* node) {
    return node && (node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE);
}

const char* attribute(const GumboNode* node, const char* name) {
    if (!isElement(node)) {
        return nullptr;
    }
    const GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, name);
    return attr ? attr->value : nullptr;
}

std::string attributeOr(const GumboNode* node, const char* name, const std::string& fallback) {
    const char* value = attribute(node, name);
    return value ? std::string(value) : fallback;
}

bool hasClass(const GumboNode* node, const std::string& name) {
    const char* value = attribute(node, "class");
    if (!value || name.empty()) {
        return false;
    }
    const char* cursor = value;
    while (*cursor) {
        while (*cursor && isSpace(static_cast<unsigned char>(*cursor))) {
            ++cursor;
        }
        const char* begin = cursor;
        while (*cursor && !isSpace(static_cast<unsigned char>(*cursor))) {
            ++cursor;
        }
        size_t length = static_cast<size_t>(cursor - begin);
        if (length == name.size() && std::memcmp(begin, name.data(), length) == 0) {
            return true;
        }
    }
    return false;
}

GumboTag tagOf(const GumboNode* node) {
    return isElement(node) ? node->v.element.tag : GUMBO_TAG_UNKNOWN;
}

const GumboNode* findFirst(const GumboNode* root, const Predicate& match) {
    std::vector<const GumboNode*> found;
    walk(root, match, found, true);
    return found.empty() ? nullptr : found.front();
}

std::vector<const GumboNode*> findAll(const GumboNode* root, const Predicate& match) {
    std::vector<const GumboNode*> found;
    walk(root, match, found, false);
    return found;
}

const GumboNode* elementById(const GumboNode* root, const std::string& id) {
    return findFirst(root, [&id](const GumboNode* node) {
        const char* value = attribute(node, "id");
        return value && id == value;
    });
}

const GumboNode* firstByTag(const GumboNode* root, GumboTag tag) {
    return findFirst(root, [tag](const GumboNode* node) { return tagOf(node) == tag; });
}

std::vector<const GumboNode*> allByTag(const GumboNode* root, GumboTag tag) {
    return findAll(root, [tag](const GumboNode* node) { return tagOf(node) == tag; });
}

std::vector<const GumboNode*> allByClass(const GumboNode* root, const std::string& name) {
    return findAll(root, [&name](const GumboNode* node) { return hasClass(node, name); });
}

std::vector<const GumboNode*> childElements(const GumboNode* node) {
    std::vector<const GumboNode*> elements;
    const GumboVector* kids = childrenOf(node);
    if (!kids) {
        return elements;
    }
    for (unsigned int i = 0; i < kids->length; ++i) {
        const GumboNode* child = static_cast<const GumboNode*>(kids->data[i]);
        if (isElement(child)) {
            elements.push_back(child);
        }
    }
    return elements;
}

std::string text(const GumboNode* node) {
    std::string raw;
    collectText(node, raw);
    return normalise(raw);
}

}
