#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <cstdio>
int main() {
 const char *values[]={"DNS:localhost,IP:127.0.0.1,IP:::1,IP:fe80::1%12", "DNS:localhost,IP:127.0.0.1,IP:::1"};
 for(int i=0;i<2;++i) {
  auto *ext=X509V3_EXT_conf_nid(nullptr,nullptr,NID_subject_alt_name,values[i]);
  std::printf("%s: %s\n", i ? "unscoped inventory" : "scoped IPv6 inventory", ext ? "valid" : "rejected");
  if(!ext) ERR_print_errors_fp(stdout);
  if(bool(ext) != bool(i)) return 1;
  X509_EXTENSION_free(ext);
 }
}
