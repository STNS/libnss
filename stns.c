#include "stns.h"
#include "toml.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

pthread_mutex_t user_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t group_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t delete_mutex = PTHREAD_MUTEX_INITIALIZER;
int highest_user_id          = 0;
int lowest_user_id           = 0;
int highest_group_id         = 0;
int lowest_group_id          = 0;

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

int stns_load_config(char *filename, stns_conf_t *c)
{
  char errbuf[200];
  const char *raw;

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

  GET_TOML_BYKEY(query_wrapper, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(chain_ssh_wrapper, toml_rtos, NULL, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(unix_socket, toml_rtos, "/var/run/stnsd.sock", TOML_STR);
  GET_TOML_BYKEY(request_timeout, toml_rtoi, 10, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(request_retry, toml_rtoi, 3, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(request_locktime, toml_rtoi, 60, TOML_NULL_OR_INT);

  GET_TOML_BYKEY(uid_shift, toml_rtoi, 0, TOML_NULL_OR_INT);
  GET_TOML_BYKEY(gid_shift, toml_rtoi, 0, TOML_NULL_OR_INT);
  fclose(fp);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] before free", __func__, __LINE__);
#endif
  toml_free(tab);
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] after free", __func__, __LINE__);
#endif
  return 0;
}

void stns_unload_config(stns_conf_t *c)
{
  UNLOAD_TOML_BYKEY(query_wrapper);
  UNLOAD_TOML_BYKEY(chain_ssh_wrapper);
  UNLOAD_TOML_BYKEY(unix_socket);
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
  char *url;
  CURL *curl;
  CURLcode result;
  char *endpoint             = "http://unix";
  struct curl_slist *headers = NULL;

  url = (char *)malloc(strlen(endpoint) + strlen(path) + 2);
  sprintf(url, "%s/%s", endpoint, path);

#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] send http request: %s", __func__, __LINE__, url);
#endif
  curl = curl_easy_init();
#ifdef DEBUG
  syslog(LOG_ERR, "%s(stns)[L%d] init http request: %s", __func__, __LINE__, url);
#endif
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, c->request_timeout);
  curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, c->unix_socket);
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
  free(url);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
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
    sprintf(c, "%s %s", cmd, arg);
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
