#include "stns.h"
#include <signal.h>
#include <getopt.h>
int main(int argc, char *argv[])
{

  int curl_result;
  stns_response_t r;
  stns_conf_t c;
  char url[MAXBUF];
  char *keys      = NULL;
  char *conf_path = NULL;
  int ret;
  int len;
  signal(SIGPIPE, SIG_IGN);

  /* Flawfinder: ignore */
  while ((ret = getopt(argc, argv, "c:")) != -1) {
    if (ret == -1)
      break;
    switch (ret) {
    case 'c':
      len = strnlen(optarg, MAXBUF) + 1;
      if (len >= MAXBUF) {
        fprintf(stderr, "conf path too long\n");
        return -1;
      }
      conf_path = optarg;
      break;
    default:
      break;
    }
  }

  if (argc == 1 || argc <= optind) {
    fprintf(stderr, "User name is a required parameter\n");
    return -1;
  }

  if (conf_path == NULL)
    ret = stns_load_config(STNS_CONFIG_FILE, &c);
  else
    ret = stns_load_config(conf_path, &c);

  if (ret != 0)
    return -1;

  if (strnlen(argv[optind], MAX_USERNAME_LENGTH) >= MAX_USERNAME_LENGTH) {
    fprintf(stderr, "user name too long\n");
    return -1;
  }
  snprintf(url, sizeof(url), "users?name=%s", argv[optind]);
  r.data      = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
  curl_result = stns_request(&c, url, &r);
  if (curl_result != CURLE_OK) {
    fprintf(stderr, "http request failed user: %s\n", argv[optind]);
    free(r.data);
    stns_unload_config(&c);
    return -1;
  }

  int i;
  int size = 0;
  int key_size;
  JSON_Object *leaf;
  JSON_Value *root = json_parse_string(r.data);

  if (root == NULL) {
    free(r.data);
    syslog(LOG_ERR, "%s(stns)[L%d] json parse error", __func__, __LINE__);
    stns_unload_config(&c);
    return -1;
  }

  JSON_Array *root_array = json_value_get_array(root);
  for (i = 0; i < json_array_get_count(root_array); i++) {
    leaf = json_array_get_object(root_array, i);
    if (leaf == NULL) {
      continue;
    }

    JSON_Array *json_keys = json_object_get_array(leaf, "keys");
    for (i = 0; i < json_array_get_count(json_keys); i++) {
      const char *key = json_array_get_string(json_keys, i);

      if (size != 0) {
        keys[size] = '\n';
        size++;
        keys[size] = '\0';
      }
      key_size = strnlen(key, STNS_MAX_BUFFER_SIZE);

      if (keys) {
        keys = (char *)realloc(keys, key_size + strnlen(keys, STNS_MAX_BUFFER_SIZE) + 2);
      } else {
        keys = (char *)malloc(strnlen(key, STNS_MAX_BUFFER_SIZE) + 2);
      }

      memcpy(&(keys[size]), key, (size_t)key_size);
      size += key_size;
    }
  }

  if (keys) {
    keys[size] = '\n';
    size++;
    keys[size] = '\0';
  }

  if (c.chain_ssh_wrapper != NULL) {
    stns_response_t cr;
    cr.data = (char *)malloc(STNS_DEFAULT_BUFFER_SIZE);
    if (stns_exec_cmd(c.chain_ssh_wrapper, argv[optind], &cr) == 0) {
      key_size = cr.size;
      keys     = (char *)realloc(keys, key_size + strnlen(keys, STNS_MAX_BUFFER_SIZE) + 1);
      int len  = strnlen(cr.data, STNS_MAX_BUFFER_SIZE);
      strncpy(&(keys[size]), cr.data, len + 1);
      size += key_size;
    }
    free(cr.data);
  }

  fprintf(stdout, "%s\n", keys);
  free(keys);
  json_value_free(root);
  stns_unload_config(&c);
  return 0;
}
