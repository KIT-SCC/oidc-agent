#include "password_store.h"
#include "list/list.h"
#include "oidc-agent/oidcp/passwords/askpass.h"
#include "oidc-agent/oidcp/passwords/keyring.h"
#include "oidc-agent/oidcp/passwords/password_handler.h"
#include "utils/crypt/passwordCrypt.h"
#include "utils/deathUtils.h"
#include "utils/listUtils.h"
#include "utils/memory.h"
#include "utils/oidc_error.h"
#include "utils/password_entry.h"
#include "utils/system_runner.h"

#include <syslog.h>
#include <time.h>

int matchPasswordEntryByShortname(struct password_entry* a,
                                  struct password_entry* b) {
  if (a == NULL && b == NULL) {
    return 1;
  }
  if (a == NULL || b == NULL) {
    return 0;
  }
  if (a->shortname == NULL && b->shortname == NULL) {
    return 1;
  }
  if (a->shortname == NULL || b->shortname == NULL) {
    return 0;
  }
  return strequal(a->shortname, b->shortname);
}

static list_t* passwords = NULL;

char* memory_getPasswordFor(const struct password_entry* pwe) {
  if (pwe == NULL) {
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  if (pwe->expires_at && pwe->expires_at < time(NULL)) {
    // Actually expired entries should already be gone from the list
    oidc_errno = OIDC_EPWNOTFOUND;
    syslog(LOG_AUTHPRIV | LOG_NOTICE, "Found an expired entry for '%s'",
           pwe->shortname);
    return NULL;
  }
  return oidc_strcopy(pwe->password);
}

void initPasswordStore() {
  if (passwords == NULL) {
    passwords        = list_new();
    passwords->free  = (void (*)(void*))_secFreePasswordEntry;
    passwords->match = (int (*)(void*, void*))matchPasswordEntryByShortname;
  }
}

oidc_error_t savePassword(struct password_entry* pw) {
  if (pw == NULL) {
    oidc_setArgNullFuncError(__func__);
    return oidc_errno;
  }
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Saving password for '%s'", pw->shortname);
  initPasswordStore();
  if (pw->password) {  // For prompt and command password won't be set
    char* tmp = encryptPassword(pw->password, pw->shortname);
    if (tmp == NULL) {
      return oidc_errno;
    }
    pwe_setPassword(pw, tmp);
  }
  if (pw->type & PW_TYPE_MNG) {
    keyring_savePasswordFor(pw->shortname, pw->password);
  }
  list_removeIfFound(
      passwords,
      pw);  // Removing an existing (old) entry for the same shortname -> update
  list_rpush(passwords, list_node_new(pw));
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Now there are %d passwords saved",
         passwords->len);
  return OIDC_SUCCESS;
}

oidc_error_t removeOrExpirePasswordFor(const char* shortname, int remove) {
  if (shortname == NULL) {
    oidc_setArgNullFuncError(__func__);
    return oidc_errno;
  }
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "%s password for '%s'",
         remove ? "Removing" : "Expiring", shortname);
  if (passwords == NULL) {
    return OIDC_SUCCESS;
  }
  struct password_entry key  = {.shortname = oidc_strcopy(shortname)};
  list_node_t*          node = findInList(passwords, &key);
  secFree(key.shortname);
  if (node == NULL) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "No password found for '%s'", shortname);
    return OIDC_SUCCESS;
  }
  struct password_entry* pw   = node->val;
  unsigned char          type = pw->type;
  if (type & PW_TYPE_MNG) {
    keyring_removePasswordFor(shortname);
  }
  if (remove) {
    list_remove(passwords, node);
  } else {
    pwe_setPassword(pw, NULL);
    pwe_setExpiresAt(pw, 0);
  }
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Now there are %d passwords saved",
         passwords->len);
  return OIDC_SUCCESS;
}

oidc_error_t removePasswordFor(const char* shortname) {
  return removeOrExpirePasswordFor(shortname, 1);
}

oidc_error_t expirePasswordFor(const char* shortname) {
  return removeOrExpirePasswordFor(shortname, 0);
}

oidc_error_t removeAllPasswords() {
  if (passwords == NULL) {
    return OIDC_SUCCESS;
  }
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Removing all passwords");
  list_node_t*     node;
  list_iterator_t* it = list_iterator_new(passwords, LIST_HEAD);
  while ((node = list_iterator_next(it))) {
    struct password_entry* pwe = node->val;
    keyring_removePasswordFor(pwe->shortname);
  }
  list_iterator_destroy(it);
  secFreeList(passwords);
  passwords = NULL;
  return OIDC_SUCCESS;
}

char* getPasswordFor(const char* shortname) {
  if (shortname == NULL) {
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  if (passwords == NULL) {
    oidc_errno = OIDC_EPWNOTFOUND;
    return NULL;
  }
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Getting password for '%s'", shortname);
  struct password_entry key  = {.shortname = oidc_strcopy(shortname)};
  list_node_t*          node = findInList(passwords, &key);
  secFree(key.shortname);
  if (node == NULL) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "No password found for '%s'", shortname);
    return OIDC_SUCCESS;
  }
  struct password_entry* pw   = node->val;
  unsigned char          type = pw->type;
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Password type is %hhu", type);
  char* res = NULL;
  if (!res && type & PW_TYPE_MEM) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Try getting password from memory");
    char* crypt = memory_getPasswordFor(pw);
    res         = decryptPassword(crypt, shortname);
    secFree(crypt);
  }
  if (!res && type & PW_TYPE_MNG) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Try getting password from keyring");
    char* crypt = keyring_getPasswordFor(shortname);
    res         = decryptPassword(crypt, shortname);
    secFree(crypt);
  }
  if (!res && type & PW_TYPE_CMD) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Try getting password from command");
    res = getOutputFromCommand(pw->command);
  }
  if (!res && type & PW_TYPE_PRMT) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Try getting password from user prompt");
    res = askpass_getPasswordForUpdate(shortname);
    if (res && type & PW_TYPE_MEM) {
      pwe_setPassword(pw, encryptPassword(res, shortname));
    }
  }
  return res;
}

time_t getMinPasswordDeath() {
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Getting min death time for passwords");
  return getMinDeathFrom(passwords, (time_t(*)(void*))pwe_getExpiresAt);
}

struct password_entry* getDeathPasswordEntry() {
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Searching for death passwords");
  return getDeathElementFrom(passwords, (time_t(*)(void*))pwe_getExpiresAt);
}

void removeDeathPasswords() {
  struct password_entry* death_pwe = NULL;
  while ((death_pwe = getDeathPasswordEntry()) != NULL) {
    expirePasswordFor(death_pwe->shortname);
  }
}