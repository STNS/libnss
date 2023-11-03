#include "stns.h"
#include "stns_test.h"

stns_conf_t test_conf()
{
  stns_conf_t c;
  c.api_endpoint       = "http://httpbin";
  c.http_proxy         = NULL;
  c.cache_dir          = "/var/cache/stns";
  c.cached_unix_socket = "/var/run/cache-stnsd.sock";
  c.cache              = 0;
  c.user               = NULL;
  c.ssl_verify         = 0;
  c.use_cached         = 0;
  c.cached_enable      = 0;
  c.password           = NULL;
  c.query_wrapper      = NULL;
  c.tls_cert           = NULL;
  c.tls_key            = NULL;
  c.tls_ca             = NULL;
  c.http_headers       = NULL;
  c.request_timeout    = 3;
  c.request_retry      = 3;
  c.auth_token         = NULL;
  return c;
}

void readfile(char *file, char **result)
{
  FILE *fp;
  char buff[MAXBUF];
  int len;
  int total;

  fp = fopen(file, "r");
  if (fp == NULL) {
    return;
  }

  total   = 0;
  for (;;) {
    if (fgets(buff, MAXBUF, fp) == NULL) {
      break;
    }
    len = strnlen(buff, STNS_MAX_BUFFER_SIZE);

    if (*result == NULL) {
      break;
    }
    strcpy(*result + total, buff);
    total += len;
  }
  fclose(fp);
}

Test(stns_load_config, load_ok)
{
  char *f = "test/stns.conf";
  stns_conf_t c;
  stns_load_config(f, &c);

  cr_assert_str_eq(c.api_endpoint, "http://<server-ip>:1104/v1");
  cr_assert_str_eq(c.auth_token, "xxxxxxxxxxxxxxx");
  cr_assert_str_eq(c.user, "test_user");
  cr_assert_str_eq(c.password, "test_password");
  cr_assert_str_eq(c.chain_ssh_wrapper, "/usr/libexec/openssh/ssh-ldap-wrapper");
  cr_assert_str_eq(c.query_wrapper, "/usr/local/bin/stns-wrapper");
  cr_assert_str_eq(c.cached_unix_socket, "/var/run/cache-stnsd.sock");
  cr_assert_str_eq(c.http_proxy, "http://your.proxy.com");
  cr_assert_eq(c.ssl_verify, 1);
  cr_assert_eq(c.cached_enable, 1);
  cr_assert_eq(c.uid_shift, 1000);
  cr_assert_eq(c.gid_shift, 2000);
  cr_assert_eq(c.request_timeout, 3);
  cr_assert_eq(c.request_retry, 3);
  cr_assert_eq(c.negative_cache_ttl, 10);
  cr_assert_str_eq(c.tls_cert, "example_cert");
  cr_assert_str_eq(c.tls_key, "example_key");
  cr_assert_str_eq(c.tls_ca, "ca_cert");
  cr_assert_eq(c.http_headers->size, 1);
  cr_assert_str_eq(c.http_headers->headers[0].key, "X-API-TOKEN");
  cr_assert_str_eq(c.http_headers->headers[0].value, "token");
  stns_unload_config(&c);
}

Test(stns_request, http_request)
{
  char expect_body[1024];
  stns_conf_t c = test_conf();
  stns_response_t r;
  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "user-agent", &r);

  snprintf(expect_body, sizeof(expect_body), "{\n  \"user-agent\": \"%s\"\n}\n", STNS_VERSION_WITH_NAME);
  cr_assert_str_eq(r.data, expect_body);
  free(r.data);
}

Test(stns_request, http_cache)
{
  struct stat st;
  stns_conf_t c = test_conf();
  stns_response_t r;
  char path[MAXBUF];
  snprintf(path, sizeof(path), "/var/cache/stns/%d/%s", geteuid(), "get%3Fexample");

  unlink(path);
  c.cache              = 1;
  c.cache_ttl          = 1;
  c.negative_cache_ttl = 1;
  c.use_cached         = 0;
  c.cached_enable      = 0;

  mkdir("/var/cache/stns/", S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "get?notfound", &r);
  cr_assert_eq(stat(path, &st), -1);
  free(r.data);

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "get?example", &r);
  cr_assert_eq(stat(path, &st), 0);
  free(r.data);
  sleep(2);

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "get?notfound", &r);
  cr_assert_eq(stat(path, &st), -1);
  free(r.data);
}

Test(stns_request, http_notfound)
{
  struct stat st;
  stns_conf_t c = test_conf();
  stns_response_t r;
  c.cache = 0;

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  cr_assert_eq(stns_request(&c, "status/404", &r), CURLE_HTTP_RETURNED_ERROR);
  free(r.data);
}

Test(stns_request, wrapper_request_ok)
{
  stns_conf_t c = test_conf();
  stns_response_t r;
  int res;

  c.query_wrapper = "test/dummy_arg.sh";
  c.cache_dir     = "/var/cache/stns";
  c.cache         = 0;

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  res = stns_request(&c, "users?name=test", &r);
  cr_assert_str_eq(r.data, "ok\n");
  cr_assert_eq(res, 0);
  free(r.data);
}

Test(stns_request, wrapper_request_ng)
{
  stns_conf_t c = test_conf();
  stns_response_t r;
  int res;

  c.cache         = 0;
  c.query_wrapper = "test/dummy_arg.sh";

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  res = stns_request(&c, NULL, &r);
  cr_assert_eq(res, 22);
  free(r.data);
}

Test(stns_request, http_request_with_header)
{
  stns_conf_t c = test_conf();
  stns_response_t r;

  c.http_headers                       = (stns_user_httpheaders_t *)malloc(sizeof(stns_user_httpheaders_t));
  stns_user_httpheader_t *http_headers = (stns_user_httpheader_t *)malloc(sizeof(stns_user_httpheader_t) * 2);
  http_headers[0].key                  = "test_key1";
  http_headers[0].value                = "test_value1";
  http_headers[1].key                  = "test_key2";
  http_headers[1].value                = "test_value2";
  c.http_headers->headers              = http_headers;
  c.http_headers->size                 = 2;

  c.request_timeout = 3;
  c.request_retry   = 3;
  c.auth_token      = NULL;
  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "headers", &r);

  cr_assert(strstr(r.data, "\"Test-Key1\": \"test_value1\""));
  free(r.data);
  free(c.http_headers);
  free(http_headers);
}

Test(stns_request_available, ok)
{
  char expect_body[1024];
  stns_conf_t c;
  stns_response_t r;

  c.request_locktime = 1;
  stns_make_lockfile(STNS_LOCK_FILE);
  cr_assert_eq(stns_request_available(STNS_LOCK_FILE, &c), 0);
  sleep(3);
  cr_assert_eq(stns_request_available(STNS_LOCK_FILE, &c), 1);
}

Test(stns_exec_cmd, ok)
{
  char expect_body[1024];
  stns_response_t r;
  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  int ret = stns_exec_cmd("test/dummy.sh", "users?name=test", &r);

  cr_assert_eq(ret, 0);
  cr_expect_str_eq(r.data, "aaabbbccc\nddd\n");
  free(r.data);
}

Test(query_available, ok)
{
  set_user_highest_id(10);
  set_user_lowest_id(3);
  set_group_highest_id(10);
  set_group_lowest_id(3);

  cr_assert_eq(stns_user_highest_query_available(1), 1);
  cr_assert_eq(stns_user_highest_query_available(11), 0);
  cr_assert_eq(stns_user_lowest_query_available(4), 1);
  cr_assert_eq(stns_user_lowest_query_available(2), 0);

  cr_assert_eq(stns_group_highest_query_available(1), 1);
  cr_assert_eq(stns_group_highest_query_available(11), 0);
  cr_assert_eq(stns_group_lowest_query_available(4), 1);
  cr_assert_eq(stns_group_lowest_query_available(2), 0);
}

Test(stns_request, http_request_with_cached)
{
  char expect_body[1024];
  stns_conf_t c   = test_conf();
  c.use_cached    = 1;
  c.cached_enable = 1;
  stns_response_t r;
  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  stns_request(&c, "user-agent", &r);

  snprintf(expect_body, sizeof(expect_body), "{\n  \"user-agent\": \"%s\"\n}\n", "libstns-go/0.0.1");
  cr_assert_str_eq(r.data, expect_body);
  free(r.data);
}

Test(stns_request, http_request_notfound_with_cached)
{
  char expect_body[1024];
  stns_conf_t c   = test_conf();
  c.use_cached    = 1;
  c.cached_enable = 1;
  stns_response_t r;

  r.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  cr_assert_eq(stns_request(&c, "status/404", &r), CURLE_HTTP_RETURNED_ERROR);
  free(r.data);
}

Test(username_validation, valid_usernames) {
    cr_assert(is_valid_username("username") == 0);
    cr_assert(is_valid_username("user-name") == 0);
    cr_assert(is_valid_username("username1") == 0);
}

Test(username_validation, invalid_usernames) {
    cr_assert(is_valid_username("Username") == 1); // contains uppercase
    cr_assert(is_valid_username("username!") == 1); // contains special char
    cr_assert(is_valid_username("1username") == 1); // starts with digit
    cr_assert(is_valid_username("") == 1); // empty string
    cr_assert(is_valid_username(NULL) == 1); // NULL
    char long_username[MAX_USERNAME_LENGTH + 2];
    memset(long_username, 'a', sizeof(long_username) - 1);
    long_username[sizeof(long_username) - 1] = '\0';
    cr_assert(is_valid_username(long_username) == 1); // too long
}

Test(groupname_validation, valid_groupnames) {
    cr_assert(is_valid_groupname("groupname") == 0);
    cr_assert(is_valid_groupname("group-name") == 0);
    cr_assert(is_valid_groupname("groupname1") == 0);
}

Test(groupname_validation, invalid_groupnames) {
    cr_assert(is_valid_groupname("Groupname") == 1); // contains uppercase
    cr_assert(is_valid_groupname("groupname!") == 1); // contains special char
    cr_assert(is_valid_groupname("1groupname") == 1); // starts with digit
    cr_assert(is_valid_groupname("") == 1); // empty string
    cr_assert(is_valid_groupname(NULL) == 1); // NULL
    // generate an overly long groupname
    char long_groupname[MAX_GROUPNAME_LENGTH + 2];
    memset(long_groupname, 'a', sizeof(long_groupname) - 1);
    long_groupname[sizeof(long_groupname) - 1] = '\0';
    cr_assert(is_valid_groupname(long_groupname) == 1); // too long
}
