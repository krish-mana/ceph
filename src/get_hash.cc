// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-

#include "stdint.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "include/ceph_hash.h"

int main(int argc, char **argv) {
  if (argc < 2)
    exit(0);
  uint32_t ps = 0;
  ps = ceph_str_hash(CEPH_STR_HASH_RJENKINS, argv[1], strlen(argv[1]));
  printf("Hash is %x\n", ps);
  return 0;
}
