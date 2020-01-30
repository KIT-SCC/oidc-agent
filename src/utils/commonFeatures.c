#include "commonFeatures.h"
#include "defines/ipc_values.h"
#include "ipc/cryptCommunicator.h"
#include "list/list.h"
#include "utils/file_io/fileUtils.h"
#include "utils/listUtils.h"
#include "utils/memory.h"
#include "utils/oidc_error.h"
#include "utils/printer.h"

void common_handleListConfiguredAccountConfigs() {
  list_t* list = getAccountConfigFileList();
  list_mergeSort(list, (int (*)(const void*, const void*))compareFilesByName);
  char* str = listToDelimitedString(list, '\n');
  list_destroy(list);
  printStdout("%s\n", str);
  secFree(str);
}

void common_assertAgent() {
  char* res = ipc_cryptCommunicate(REQUEST_CHECK);
  if (res == NULL) {
    oidc_perror();
    exit(EXIT_FAILURE);
  }
  secFree(res);
}
