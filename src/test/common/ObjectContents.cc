// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "ObjectContents.h"
#include "include/buffer.h"

ObjectContents *object_decode(bufferlist::iterator &bp)
{
  __u8 code = *bp;
  ObjectContents *new_obj = 0;
  switch (code) {
  case RANDOMWRITEFULL:
    new_obj = new RandomWriteFull(bp);
    break;
  case EMPTYFILE:
    new_obj = new EmptyFile(bp);
    break;
  case DELETED:
    new_obj = new Deleted(bp);
    break;
  case CLONERANGE:
    new_obj = new CloneRange(bp);
    break;
  defaut:
    assert(0);
  }
  return new_obj;
}
