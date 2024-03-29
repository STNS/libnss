#include "stns.h"

static JSON_Value *entries  = NULL;
static int entry_idx        = 0;
pthread_mutex_t spent_mutex = PTHREAD_MUTEX_INITIALIZER;

#define SHADOW_ENSURE(entry)                                                                                           \
  const char *name     = json_value_get_string(json_object_get_value(entry, "name"));                                  \
  const char *password = json_value_get_string(json_object_get_value(entry, "password"));                              \
  SET_ATTRBUTE(sp, name, namp);                                                                                        \
  STNS_SET_DEFAULT_VALUE(pw, password, "!!");                                                                          \
  SET_ATTRBUTE(sp, password, pwdp);                                                                                    \
  rbuf->sp_lstchg = -1;                                                                                                \
  rbuf->sp_min    = -1;                                                                                                \
  rbuf->sp_max    = -1;                                                                                                \
  rbuf->sp_warn   = -1;                                                                                                \
  rbuf->sp_inact  = -1;                                                                                                \
  rbuf->sp_expire = -1;                                                                                                \
  rbuf->sp_flag   = ~0ul;

STNS_ENSURE_BY(name, const char *, user_name, string, name, (strcmp(current, user_name) == 0), spwd, SHADOW)
STNS_ENSURE_BY(uid, uid_t, uid, number, id, current + (c->uid_shift) == uid, spwd, SHADOW)
STNS_GET_SINGLE_VALUE_METHOD(getspnam_r, const char *name, "users?name=%s", name, spwd, USER_NAME_QUERY_AVAILABLE, )
STNS_GET_SINGLE_VALUE_METHOD(getspuid_r, uid_t uid, "users?id=%d", uid, spwd, , -(c.uid_shift))
STNS_SET_ENTRIES(sp, SHADOW, spwd, users)
