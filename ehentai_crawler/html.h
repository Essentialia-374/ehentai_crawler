#pragma once

#include <gumbo.h>
#include <functional>
#include <string>
#include <vector>

// Thin, allocation friendly helpers on top of gumbo-parser. Every returned node
// points into the owning Document and stays valid only as long as it lives.
namespace html {

class Document {
public:
    explicit Document(const std::string& source);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&& other) noexcept;
    Document& operator=(Document&& other) noexcept;

    bool valid() const { return output_ != nullptr; }
    const GumboNode* root() const { return output_ ? output_->root : nullptr; }

private:
    GumboOutput* output_;
};

using Predicate = std::function<bool(const GumboNode*)>;

bool isElement(const GumboNode* node);
const char* attribute(const GumboNode* node, const char* name);
std::string attributeOr(const GumboNode* node, const char* name, const std::string& fallback = std::string());
bool hasClass(const GumboNode* node, const std::string& name);
GumboTag tagOf(const GumboNode* node);

const GumboNode* findFirst(const GumboNode* root, const Predicate& match);
std::vector<const GumboNode*> findAll(const GumboNode* root, const Predicate& match);

const GumboNode* elementById(const GumboNode* root, const std::string& id);
const GumboNode* firstByTag(const GumboNode* root, GumboTag tag);
std::vector<const GumboNode*> allByTag(const GumboNode* root, GumboTag tag);
std::vector<const GumboNode*> allByClass(const GumboNode* root, const std::string& name);

// Direct element children, in document order.
std::vector<const GumboNode*> childElements(const GumboNode* node);

// Every descendant text node joined together, with runs of whitespace collapsed
// and the result trimmed. Script and style contents are ignored.
std::string text(const GumboNode* node);

}
