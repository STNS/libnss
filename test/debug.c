
#define DEBUG 1
#include <stdio.h>
#include "../stns.h"

int main(void)
{
  stns_conf_t c;
  c.api_endpoint    = "https://httpbin.org";
  c.http_proxy      = NULL;
  c.cache_dir       = "/var/cache/stns";
  c.unix_socket     = "/var/run/cache-stnsd.sock";
  c.cache           = 0;
  c.user            = NULL;
  c.ssl_verify      = 0;
  c.use_cached      = 0;
  c.password        = NULL;
  c.query_wrapper   = NULL;
  c.tls_cert        = NULL;
  c.tls_key         = NULL;
  c.tls_ca          = NULL;
  c.http_headers    = NULL;
  c.request_timeout = 3;
  c.request_retry   = 3;
  c.auth_token      = NULL;
  struct stat st;
  stns_response_t r;
  char path[MAXBUF];
  snprintf(path, sizeof(path), "/var/cache/stns/%d/%s", geteuid(), "get%3Fexample");

  c.cache              = 1;
  c.cache_ttl          = 1;
  c.negative_cache_ttl = 1;
  c.use_cached         = 0;

  r->data = (char *)malloc(sizeof(char));
  stns_request(&c, "get?example", &r);
  free(r.data);
  sleep(2);
  // deleted by thread
  r->data = (char *)malloc(sizeof(char));
  stns_request(&c, "get?notfound", &r);
  free(r.data);
  return 0;
}
