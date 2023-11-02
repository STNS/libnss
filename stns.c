#include "stns.h"
#include "toml.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

pthread_mutex_t user_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t group_mutex = PTHREAD_MUTEX_INITIALIZER;
int highest_user_id         = 0;
int lowest_user_id          = 0;
int highest_group_id        = 0;
int lowest_group_id         = 0;

SET_GET_HIGH_LOW_ID(highest, user);
SET_GET_HIGH_LOW_ID(lowest, user);
SET_GET_HIGH_LOW_ID(highest, group);
SET_GET_HIGH_LOW_ID(lowest, group);

ID_QUERY_AVAILABLE(user, high, <)
ID_QUERY_AVAILABLE(user, low, >)
ID_QUERY_AVAILABLE(group, high, <)
ID_QUERY_AVAILABLE(group, low, >)

#define TRIM_SLASH(key)                                                                                                \
  if (c->key != NULL) {                                                                                                \
    const int key##_len = strlen(c->key);                                                                              \
    if (key##_len > 0) {                                                                                               \
      if (c->key[key##_len - 1] == '/') {                                                                              \
        c->key[key##_len - 1] = '\0';                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
  }

static void stns_force_create_cache_dir(stns_conf_t *c)
{
  if (c->cache && geteuid() == 0 && !c->cached_enable) {
    struct stat statBuf;

    char path[MAXBUF];
    snprintf(path, sizeof(path), "%s", c->cache_dir);
    mode_t um = {0};
    um        = umask(0);
    if (stat(path, &statBuf) != 0) {
      mkdir(path, S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
    } else if ((S_ISVTX & statBuf.st_mode) == 0) {
      chmod(path, S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
    }
    umask(um);
  }
}

int stns_load_config(char *filename, stns_conf_t *c)
{
  char errbuf[200];
  const char *raw, *key;
  toml_table_t *in_tab;

#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] start load config", __func__, __LINE__);
#endif
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    syslog(LOG_ERR, "%s(stns)[L%d] cannot open %s: %s", __func__, __LINE__, filename, strerror(errno));
    return 1;
  }

  toml_table_t *tab = toml_parse_file(fp, errbuf, sizeof(errbuf));

  if (!tab) {
    syslog(LOG_ERR, "%s(stns)[L%d] %s", __func__, __LINE__, errbuf);
    return 1;
  }

  GET_TOML_BYKEY(api_endpoint, toml_rtos, "http://localhost:1104/v1", TOML_STR);
  GET_TOML_BYKEY(cache_dir, toml_rtos, "/var/cache/stns", TOML_STR);
  GET_TOML_BYKEY(auth_token, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(user, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(password, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(query_wrapper, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(chain_ssh_wrapper, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(use_cached, toml_rtob, 0, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(http_proxy, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BY_TABLE_KEY(tls, key, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BY_TABLE_KEY(tls, cert, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BY_TABLE_KEY(tls, ca, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BY_TABLE_KEY(cached, enable, toml_rtob, 0, TOML_NULL_OR_INT);
  GET_TOML_BY_TABLE_KEY(cached, unix_socket, toml_rtos, "/var/run/cache-stnsd.sock", TOML_STR);

  GET_TOML_BYKEY(uid_shift, toml_rtoi, 0, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(gid_shift, toml_rtoi, 0, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(cache_ttl, toml_rtoi, 600, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(negative_cache_ttl, toml_rtoi, 10, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(ssl_verify, toml_rtob, 1, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(cache, toml_rtob, 1, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(request_timeout, toml_rtoi, 10, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(request_retry, toml_rtoi, 3, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(request_locktime, toml_rtoi, 60, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(http_location, toml_rtob, 0, TOML_NULL_OR_INT);

  TRIM_SLASH(api_endpoint)
  TRIM_SLASH(cache_dir)

  int header_size                      = 0;
  stns_user_httpheader_t *http_headers = NULL;

  if (0 != (in_tab = toml_table_in(tab, "http_headers"))) {
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] before malloc", __func__, __LINE__);
#endif
    c->http_headers = (stns_user_httpheaders_t *)malloc(sizeof(stns_user_httpheaders_t));
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] after malloc", __func__, __LINE__);
#endif

    while (1) {
      if (header_size > MAXBUF)
        break;

      if (0 != (key = toml_key_in(in_tab, header_size)) && 0 != (raw = toml_raw_in(in_tab, key))) {
        if (header_size == 0)
          http_headers = (stns_user_httpheader_t *)malloc(sizeof(stns_user_httpheader_t));
        else
          http_headers =
              (stns_user_httpheader_t *)realloc(http_headers, sizeof(stns_user_httpheader_t) * (header_size + 1));

        if (0 != toml_rtos(raw, &http_headers[header_size].value)) {
          syslog(LOG_ERR, "%s(stns)[L%d] cannot parse toml file:%s key:%s", __func__, __LINE__, filename, key);
        }
        http_headers[header_size].key = strdup(key);
        header_size++;
      } else {
        break;
      }
    }
    c->http_headers->headers = http_headers;
    c->http_headers->size    = (size_t)header_size;

  } else {
    c->http_headers = NULL;
  }
  stns_force_create_cache_dir(c);
  fclose(fp);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  toml_free(tab);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif

  if (c->use_cached) {
    c->cached_enable = 1;
  }
  return 0;
}

void stns_unload_config(stns_conf_t *c)
{
  UNLOAD_TOML_BYKEY(api_endpoint);
  UNLOAD_TOML_BYKEY(cache_dir);
  UNLOAD_TOML_BYKEY(auth_token);
  UNLOAD_TOML_BYKEY(user);
  UNLOAD_TOML_BYKEY(password);
  UNLOAD_TOML_BYKEY(query_wrapper);
  UNLOAD_TOML_BYKEY(chain_ssh_wrapper);
  UNLOAD_TOML_BYKEY(http_proxy);
  UNLOAD_TOML_BYKEY(tls_cert);
  UNLOAD_TOML_BYKEY(tls_key);
  UNLOAD_TOML_BYKEY(tls_ca);
  UNLOAD_TOML_BYKEY(cached_unix_socket);

  if (c->http_headers != NULL) {
    int i = 0;
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
    for (i = 0; i < c->http_headers->size; i++) {
      free(c->http_headers->headers[i].value);
      free(c->http_headers->headers[i].key);
      free(c->http_headers->headers);
    }
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  }
  UNLOAD_TOML_BYKEY(http_headers);
}

static void trim(char *s)
{
  int i, j;

  for (i = strlen(s) - 1; i >= 0 && isspace(s[i]); i--)
    ;
  s[i + 1] = '\0';
  for (i = 0; isspace(s[i]); i++)
    ;
  if (i > 0) {
    j = 0;
    while (s[i])
      s[j++] = s[i++];
    s[j] = '\0';
  }
}

#define SET_TRIM_ID(high_or_low, user_or_group, short_name)                                                            \
  tp = strtok(NULL, ".");                                                                                              \
  trim(tp);                                                                                                            \
  set_##user_or_group##_##high_or_low##est_id(atoi(tp) + c->short_name##id_shift);

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{

  stns_conf_t *c = (stns_conf_t *)userdata;
  char *tp;
  tp = strtok(buffer, ":");
  if (strcmp(tp, "User-Highest-Id") == 0) {
    SET_TRIM_ID(high, user, u)
  } else if (strcmp(tp, "User-Lowest-Id") == 0) {
    SET_TRIM_ID(low, user, u)
  } else if (strcmp(tp, "Group-Highest-Id") == 0) {
    SET_TRIM_ID(high, group, g)
  } else if (strcmp(tp, "Group-Lowest-Id") == 0) {
    SET_TRIM_ID(low, group, g)
  }

  return nitems * size;
}

// base https://github.com/linyows/octopass/blob/master/octopass.c
// size is always 1
static size_t response_callback(void *buffer, size_t size, size_t nmemb, void *userp)
{
  size_t segsize       = size * nmemb;
  stns_response_t *res = (stns_response_t *)userp;

  if (segsize > STNS_MAX_BUFFER_SIZE) {
    syslog(LOG_ERR, "%s(stns)[L%d] Response is too large", __func__, __LINE__);
    return 0;
  }

  res->data = (char *)realloc(res->data, res->size + segsize + 1);

  if (res->data) {
    memcpy(&(res->data[res->size]), buffer, segsize);
    res->size += segsize;
    res->data[res->size] = 0;
  }
  return segsize;
}

// base https://github.com/linyows/octopass/blob/master/octopass.c
static CURLcode inner_http_request(stns_conf_t *c, char *path, stns_response_t *res)
{
  char *auth       = NULL;
  char *in_headers = NULL;
  char *url;
  CURL *curl;
  CURLcode result;
  struct curl_slist *headers = NULL;
  size_t len;
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] send http request: %s", __func__, __LINE__, url);
#endif
  curl = curl_easy_init();
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] init http request: %s", __func__, __LINE__, url);
#endif

  if (!c->cached_enable) {
    if (c->auth_token != NULL) {
      len  = strlen(c->auth_token) + 22;
      auth = (char *)malloc(len);
      snprintf(auth, len, "Authorization: token %s", c->auth_token);
    } else {
      auth = NULL;
    }

    len = strlen(c->api_endpoint) + strlen(path) + 2;
    url = (char *)malloc(strlen(c->api_endpoint) + strlen(path) + 2);
    snprintf(url, len, "%s/%s", c->api_endpoint, path);

    if (auth != NULL) {
      headers = curl_slist_append(headers, auth);
    }

    if (c->http_headers != NULL) {

      int i, size = 0;
      for (i = 0; i < c->http_headers->size; i++) {
        size += strlen(c->http_headers->headers[i].key) + strlen(c->http_headers->headers[i].value) + 3;
        if (in_headers == NULL)
          in_headers = (char *)malloc(size);
        else
          in_headers = (char *)realloc(in_headers, size);

        snprintf(in_headers, strlen(c->http_headers->headers[i].key) + strlen(c->http_headers->headers[i].value) + 3,
                 "%s: %s", c->http_headers->headers[i].key, c->http_headers->headers[i].value);
        headers = curl_slist_append(headers, in_headers);
      }
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, c->ssl_verify);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, c->ssl_verify);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, STNS_VERSION_WITH_NAME);
    if (c->http_location == 1) {
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    }
    if (c->tls_cert != NULL && c->tls_key != NULL) {
      curl_easy_setopt(curl, CURLOPT_SSLCERT, c->tls_cert);
      curl_easy_setopt(curl, CURLOPT_SSLKEY, c->tls_key);
    }

    if (c->tls_ca != NULL) {
      curl_easy_setopt(curl, CURLOPT_CAINFO, c->tls_ca);
    }

    if (c->user != NULL) {
      curl_easy_setopt(curl, CURLOPT_USERNAME, c->user);
    }

    if (c->password != NULL) {
      curl_easy_setopt(curl, CURLOPT_PASSWORD, c->password);
    }

    if (c->http_proxy != NULL) {
      curl_easy_setopt(curl, CURLOPT_PROXY, c->http_proxy);
    }
  } else {
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, c->cached_unix_socket);
    url = (char *)malloc(strlen("http://unix") + strlen(path) + 2);
    snprintf(url, strlen("http://unix") + strlen(path) + 2, "%s/%s", "http://unix", path);
  }
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, c->request_timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, res);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, c);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before request http request: %s", __func__, __LINE__, url);
#endif
  result = curl_easy_perform(curl);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after request http request: %s", __func__, __LINE__, url);
#endif

  long code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] get result  http request: %s", __func__, __LINE__, url);
#endif
  if (code >= 400 || code == 0) {
    if (code != 404)
      syslog(LOG_ERR, "%s(stns)[L%d] http request failed: %s code:%ld", __func__, __LINE__, curl_easy_strerror(result),
             code);
    res->data        = NULL;
    res->size        = 0;
    res->status_code = code;
    if (code != 0)
      result = CURLE_HTTP_RETURNED_ERROR;
  }

#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  if (!c->cached_enable) {
    if (c->auth_token != NULL) {
      free(auth);
    }
    if (c->http_headers != NULL) {
      free(in_headers);
    }
    curl_slist_free_all(headers);
  }

  free(url);
  curl_easy_cleanup(curl);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  return result;
}

int stns_request_available(char *path, stns_conf_t *c)
{
  struct stat st;
  if (stat(path, &st) != 0) {
    return 1;
  }

  unsigned long now  = time(NULL);
  unsigned long diff = now - st.st_ctime;
  if (diff > c->request_locktime) {
    remove(path);
    return 1;
  }
  return 0;
}

void stns_make_lockfile(char *path)
{
  FILE *fp;
  fp = fopen(path, "w");
  if (fp) {
    fclose(fp);
  }
}

// base: https://github.com/linyows/octopass/blob/master/octopass.c
void stns_export_file(char *dir, char *file, char *data)
{
  struct stat statbuf;
  if (stat(dir, &statbuf) != 0) {
    mode_t um = {0};
    um        = umask(0);
    mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR);
    umask(um);
  }

  if (stat(file, &statbuf) == 0 && statbuf.st_uid != geteuid()) {
    return;
  }

  FILE *fp = fopen(file, "w");
  if (!fp) {
    syslog(LOG_ERR, "%s(stns)[L%d] cannot open %s", __func__, __LINE__, file);
    return;
  }
  if (data != NULL) {
    fprintf(fp, "%s", data);
  }
  fclose(fp);

  mode_t um = {0};
  um        = umask(0);
  chmod(file, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH);
  umask(um);
}

// base: https://github.com/linyows/octopass/blob/master/octopass.c
int stns_import_file(char *file, stns_response_t *res)
{
  FILE *fp = fopen(file, "r");
  if (!fp) {
    syslog(LOG_ERR, "%s(stns)[L%d] cannot open %s", __func__, __LINE__, file);
    return 0;
  }

  char buf[MAXBUF];
  int total_len = 0;
  int len       = 0;

  while (fgets(buf, sizeof(buf), fp) != NULL) {
    len = strlen(buf);
    if (!res->data) {
      res->data = (char *)malloc(len + 1);
    } else {
      res->data = (char *)realloc(res->data, total_len + len + 1);
    }
    strcpy(res->data + total_len, buf);
    total_len += len;
  }
  fclose(fp);

  return 1;
}

static void delete_cache_files(stns_conf_t *c)
{
  DIR *dp;
  struct dirent *ent;
  struct stat statbuf;
  unsigned long now = time(NULL);
  char dir[MAXBUF];
  snprintf(dir, sizeof(dir), "%s/%d", c->cache_dir, geteuid());

  if ((dp = opendir(dir)) == NULL) {
    return;
  }

  char *buf = malloc(1);
  while ((ent = readdir(dp)) != NULL) {
    buf = (char *)realloc(buf, strlen(dir) + strlen(ent->d_name) + 2);
    snprintf(buf, strlen(dir) + strlen(ent->d_name) + 2, "%s/%s", dir, ent->d_name);

    if (stat(buf, &statbuf) == 0 && (statbuf.st_uid == geteuid() || geteuid() == 0)) {
      unsigned long diff = now - statbuf.st_mtime;

      if (!S_ISDIR(statbuf.st_mode) &&
          ((diff > c->cache_ttl && statbuf.st_size > 0) || (diff > c->negative_cache_ttl && statbuf.st_size == 0))) {

        if (unlink(buf) == -1) {
          syslog(LOG_ERR, "%s(stns)[L%d] cannot delete %s: %s", __func__, __LINE__, buf, strerror(errno));
        }
      }
    }
  }
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  free(buf);
  closedir(dp);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  return;
}

int stns_request(stns_conf_t *c, char *path, stns_response_t *res)
{
  CURLcode result;
  int retry_count  = c->request_retry;
  res->data        = (char *)malloc(sizeof(char));
  res->size        = 0;
  res->status_code = (long)200;

  if (path == NULL) {
    return CURLE_HTTP_RETURNED_ERROR;
  }

  char *base = curl_escape(path, strlen(path));
  char dpath[MAXBUF + 1];
  char fpath[MAXBUF * 2 + 2];
  snprintf(dpath, MAXBUF, "%s/%d", c->cache_dir, geteuid());
  snprintf(fpath, MAXBUF * 2 + 2, "%s/%s", dpath, base);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  free(base);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif

  if (c->cache && !c->cached_enable) {
    struct stat statbuf;
    if (stat(fpath, &statbuf) == 0 && statbuf.st_uid == geteuid()) {
      unsigned long now  = time(NULL);
      unsigned long diff = now - statbuf.st_mtime;

      // resource notfound
      if ((diff < c->cache_ttl && statbuf.st_size > 0) || (diff < c->negative_cache_ttl && statbuf.st_size == 0)) {
        if (statbuf.st_size == 0) {
          res->status_code = STNS_HTTP_NOTFOUND;
          return CURLE_HTTP_RETURNED_ERROR;
        }

        if (!stns_import_file(fpath, res)) {
          goto request;
        }
        res->size = strlen(res->data);
        return CURLE_OK;
      } else {
        delete_cache_files(c);
      }
    }
  }

request:
  if (!stns_request_available(STNS_LOCK_FILE, c))
    return CURLE_COULDNT_CONNECT;

  if (c->query_wrapper == NULL) {
    result = inner_http_request(c, path, res);
    while (1) {
      if (result != CURLE_OK && retry_count > 0) {
        if (result == CURLE_HTTP_RETURNED_ERROR) {
          break;
        }
        sleep(1);
        syslog(LOG_NOTICE, "%s(stns)[L%d] %d retries remaining", __func__, __LINE__, retry_count);
        result = inner_http_request(c, path, res);
        retry_count--;
      } else {
        break;
      }
    }
  } else {
    result = stns_exec_cmd(c->query_wrapper, path, res);
  }

  if (result == CURLE_COULDNT_CONNECT) {
    stns_make_lockfile(STNS_LOCK_FILE);
  }

  if (c->cache && !c->cached_enable) {
    stns_export_file(dpath, fpath, res->data);
  }
  return result;
}

unsigned int match(char *pattern, char *text)
{
  regex_t regex;
  int rc;

  if (text == NULL) {
    return 0;
  }

  rc = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
  if (rc == 0)
    rc = regexec(&regex, text, 0, 0, 0);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  regfree(&regex);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  return rc == 0;
}

int stns_exec_cmd(char *cmd, char *arg, stns_response_t *r)
{
  FILE *fp;
  char *c;

  r->data        = NULL;
  r->size        = 0;
  r->status_code = (long)200;

  if (!match("^[a-zA-Z0-9_.=\?]+$", arg)) {
    return 0;
  }

#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before malloc", __func__, __LINE__);
#endif
  if (arg != NULL) {
    c = malloc(strlen(cmd) + strlen(arg) + 2);
    snprintf(c, strlen(cmd) + strlen(arg) + 2, "%s %s", cmd, arg);
  } else {
    c = cmd;
  }
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after malloc", __func__, __LINE__);
#endif

  if ((fp = popen(c, "r")) == NULL) {
    goto err;
  }

  char buf[MAXBUF];
  int total_len = 0;
  int len       = 0;

  while (fgets(buf, sizeof(buf), fp) != NULL) {
    len = strlen(buf);
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] before malloc", __func__, __LINE__);
#endif
    if (r->data) {
      r->data = (char *)realloc(r->data, total_len + len + 1);
    } else {
      r->data = (char *)malloc(total_len + len + 1);
    }
#ifdef DEBUG
    syslog(LOG_ERR, "%s(stns)[L%d] after malloc", __func__, __LINE__);
#endif
    strcpy(r->data + total_len, buf);
    total_len += len;
  }
  pclose(fp);

  if (total_len == 0) {
    goto err;
  }
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  if (arg != NULL)
    free(c);

  r->size = total_len;
  return 0;
err:
  if (arg != NULL)
    free(c);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  return 1;
}

int is_valid_username(const char *username) {
    if (username == NULL)
    {
        return 1;
    }
    size_t len = strlen(username);

    // Check the length.
    if (len == 0 || len > MAX_USERNAME_LENGTH)
    {
        return 1;
    }

    // The first character must be a alpha.
    if (!(username[0] >= 'a' && username[0] <= 'z'))
    {
        return 1;
    }

    // The rest characters can be only alpha, digit, dash or underscore.
    for (size_t i = 1; i < len; i++)
    {
        if (!(username[i] >= 'a' && username[i] <= 'z') &&
            !(username[i] >= '0' && username[i] <= '9') &&
            username[i] != '-' && username[i] != '_')
        {
            return 1;
        }
    }

    return 0;
}

int is_valid_groupname(const char *groupname)
{
    if (groupname == NULL)
    {
        return 1;
    }
    size_t len = strlen(groupname);

    // Check the length.
    if (len == 0 || len > MAX_GROUPNAME_LENGTH)
    {
        return 1;
    }

    // The first character must be a alpha.
    if (!(groupname[0] >= 'a' && groupname[0] <= 'z'))
    {
        return 1;
    }

    // The rest characters can be only alpha, digit, dash or underscore.
    for (size_t i = 1; i < len; i++)
    {
        if (!(groupname[i] >= 'a' && groupname[i] <= 'z') &&
            !(groupname[i] >= '0' && groupname[i] <= '9') &&
            groupname[i] != '-' && groupname[i] != '_')
        {
            return 1;
        }
    }

    return 0;
}
extern int pthread_mutex_retrylock(pthread_mutex_t *mutex)
{
  int i   = 0;
  int ret = 0;
  for (;;) {
    ret = pthread_mutex_trylock(mutex);
    if (ret == 0 || i >= STNS_LOCK_RETRY)
      break;
    usleep(STNS_LOCK_INTERVAL_MSEC * 1000);
    i++;
  }
  return ret;
}
