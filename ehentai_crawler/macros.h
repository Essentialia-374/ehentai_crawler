#pragma once

#define USER_AGENT "Mozilla/5.0 (iPhone; CPU iPhone OS 18_7_8 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Mobile/15E148 Safari/604.1"

#define EH_HOST "e-hentai.org"
#define EX_HOST "exhentai.org"

#define DEFAULT_OUTPUT_DIR "downloads"
#define DEFAULT_JOBS 3
#define DEFAULT_DELAY_MS 250
#define DEFAULT_RETRIES 3
#define DEFAULT_TIMEOUT_SECONDS 60
#define DEFAULT_CONNECT_TIMEOUT_SECONDS 20

// e-hentai serves a 509 image instead of the real one once the image quota is spent.
#define QUOTA_EXCEEDED_MARKER "509.gif"
