#include <utility>
#include "VaultClient.h"

static size_t
writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  ((std::string *) userp)->append((char *) contents, size * nmemb);
  return size * nmemb;
}

Vault::HttpClient::HttpClient(Vault::Config& config)
  : debug_(config.getDebug())
  , verify_(config.getVerify())
  , connectTimeout_(config.getConnectTimeout().value)
  , errorCallback_([&](const std::string& err){})
{}

Vault::HttpClient::HttpClient(Vault::Config& config, HttpErrorCallback errorCallback) :
  debug_(config.getDebug()),
  verify_(config.getVerify()),
  connectTimeout_(config.getConnectTimeout().value),
  errorCallback_(std::move(errorCallback))
{}

bool Vault::HttpClient::is_success(std::optional<HttpResponse> response) {
  return response && response.value().statusCode.value == 200;
}

std::optional<Vault::HttpResponse>
Vault::HttpClient::get(const Vault::Url& url, const Vault::Token& token, const Vault::Namespace& ns) const {
  return executeRequest(url, token, ns, [&](CURL *curl) {});
}

std::optional<Vault::HttpResponse>
Vault::HttpClient::post(const Vault::Url& url, const Vault::Token& token, const Vault::Namespace& ns, std::string value) const {
  return executeRequest(url, token, ns, [&](CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, value.c_str());
  });
}

std::optional<Vault::HttpResponse>
Vault::HttpClient::del(const Vault::Url& url, const Vault::Token& token, const Vault::Namespace& ns) const {
  return executeRequest(url, token, ns, [&](CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  });
}

std::optional<Vault::HttpResponse>
Vault::HttpClient::list(const Vault::Url& url, const Vault::Token& token, const Vault::Namespace& ns) const {
  return executeRequest(url, token, ns, [&](CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST");
  });
}

std::optional<Vault::HttpResponse>
Vault::HttpClient::executeRequest(const Vault::Url& url,
                                  const Vault::Token& token,
                                  const Vault::Namespace& ns,
                                  const CurlSetupCallback& setupCallback) const {
  CURL *curl;
  CURLcode res = CURLE_SEND_ERROR;
  std::string buffer;
  long response_code = 0;

  curl = curl_easy_init();
  if (curl) {
    struct curl_slist *chunk = nullptr;

    if (!token.empty()) {
      chunk = curl_slist_append(chunk, ("X-Vault-Token: " + token).c_str());
    }

    if (!ns.empty()) {
      chunk = curl_slist_append(chunk, ("X-Vault-Namespace: " + ns).c_str());
    }

    chunk = curl_slist_append(chunk, "Content-Type: application/json");

    if (verify_) {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    } else {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    }

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectTimeout_);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    if (debug_) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    setupCallback(curl);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      errorCallback_(curl_easy_strerror(res));

      curl_easy_cleanup(curl);
      curl_slist_free_all(chunk);

      return std::nullopt;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
  }

  return std::optional<HttpResponse>({response_code, Vault::HttpResponseBodyString{buffer}});
}