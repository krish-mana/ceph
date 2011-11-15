#include <tr1/memory>
#include <map>

#include "include/buffer.h"
#include "test/KeyValueDB/KeyValueDBMemory.h"
#include "os/KeyValueDB.h"
#include "os/CloneableDB.h"
#include "os/CloneableAdapter.h"

#include "gtest/gtest.h"
#include "stdlib.h"

using namespace std;

class CloneableDBTest: public ::testing::Test {
public:
  boost::scoped_ptr<CloneableDB> db;

  CloneableDBTest() : db(new CloneableAdapter(new KeyValueDBMemory())) {}
};

TEST_F(CloneableDBTest, CreateOneObject) {
  map<string, bufferlist> to_set;
  to_set["test"].append(bufferptr("testval"));
  db->set("prefix", to_set);
}
