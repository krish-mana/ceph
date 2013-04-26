// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
* Ceph - scalable distributed file system
*
* Copyright (C) 2012 Inktank, Inc.
*
* This is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License version 2.1, as published by the Free Software
* Foundation. See file COPYING.
*/
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options/option.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/parsers.hpp>
#include <iostream>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <boost/scoped_ptr.hpp>

#include "os/LevelDBStore.h"

namespace po = boost::program_options;
using namespace std;

class StoreTool
{
  boost::scoped_ptr<KeyValueDB> db;

  public:
  StoreTool(const string &path) {
    LevelDBStore *db_ptr = new LevelDBStore(path);
    assert(!db_ptr->open(std::cerr));
    db.reset(db_ptr);
  }

  void list(const string &prefix) {
    KeyValueDB::WholeSpaceIterator iter = db->get_iterator();

    if (prefix.empty())
      iter->seek_to_first();
    else
      iter->seek_to_first(prefix);

    while (iter->valid()) {
      pair<string,string> rk = iter->raw_key();
      if (!prefix.empty() && (rk.first != prefix))
	break;

      std::cout << rk.first << ":" << rk.second << std::endl;
      iter->next();
    }
  }

  bool exists(const string &prefix) {
    assert(!prefix.empty());
    KeyValueDB::WholeSpaceIterator iter = db->get_iterator();
    iter->seek_to_first(prefix);
    return (iter->valid() && (iter->raw_key().first == prefix));
  }

  bool exists(const string &prefix, const string &key) {
    assert(!prefix.empty());

    if (key.empty()) {
      return exists(prefix);
    }

    bool exists = false;
    get(prefix, key, exists);
    return exists;
  }

  bufferlist get(const string &prefix, const string &key, bool &exists) {
    assert(!prefix.empty() && !key.empty());

    map<string,bufferlist> result;
    set<string> keys;
    keys.insert(key);
    db->get(prefix, keys, &result);

    if (result.count(key) > 0) {
      exists = true;
      return result[key];
    }
    exists = false;
    return bufferlist();
  }
};

void usage(const char *pname)
{
  std::cerr << "Usage: " << pname << " <store path> command [args...]\n"
    << "\n"
    << "Commands:\n"
    << "  list [prefix]\n"
    << "  exists <prefix> [key]\n"
    << "  get <prefix> <key>\n"
    << "  verify <store path>\n"
    << std::endl;
}

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");
  string store_path, cmd;
  vector<string> args;
  desc.add_options()
    ("help", "produce help message")
    ("mon-store-path", po::value<string>(&store_path),
      "path to mon directory, mandatory")
    ("command", po::value<string>(&cmd),
      "command")
    ("arg", po::value< vector<string> >(&args),
      "command")
    ("debug", "Enable diagnostic output to stderr");

  po::positional_options_description p;
  p.add("command", 1);
  p.add("args", -1);

  po::variables_map vm;
  po::store(
    po::command_line_parser(argc, argv).options(desc).positional(p).run(),
    vm);

  StoreTool st(store_path);

  if (cmd == "list") {
    string prefix;
    if (args.size() < 1) {
      usage(argv[0]);
      return 1;
    }

    st.list(args[0]);

  } else if (cmd == "exists") {
    string key;
    if (args.size() < 2) {
      usage(argv[0]);
      return 1;
    }

    bool ret = st.exists(args[0], args[1]);
    std::cout << "(" << args[0] << ", " << args[1] << ") "
	      << (ret ? "exists" : "does not exist")
	      << std::endl;
    return (ret ? 0 : 1);
  } else if (cmd == "get") {
    if (args.size() < 2) {
      usage(argv[0]);
      return 1;
    }

    bool exists = false;
    bufferlist bl = st.get(args[0], args[1], exists);
    std::cout << "(" << args[0] << ", " << args[1] << ")";
    if (!exists) {
      std::cout << " does not exist" << std::endl;
      return 1;
    }
    std::cout << std::endl;
    ostringstream os;
    bl.hexdump(os);
    std::cout << os.str() << std::endl;
  } else if (cmd == "verify") {
    assert(0);
  } else {
    std::cerr << "Unrecognized command: " << cmd << std::endl;
    return 1;
  }

  return 0;
}
