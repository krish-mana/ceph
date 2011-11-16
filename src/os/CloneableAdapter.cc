// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "CloneableAdapter.h"
#include "include/encoding.h"
#include <errno.h>
#include <map>
#include <set>
#include <string>
using namespace std;

const string STATUS_KEY = "STATUS";

static string leveled_prefix(const string &prefix, size_t level) {
  char buf[sizeof(level)*3]; // Big enough for size_t!
  snprintf(buf, sizeof(buf), "%u", (unsigned int)level);
  string out_prefix = "";
  for (string::const_iterator i = prefix.begin();
       i != prefix.end();
       ++i) {
    if (*i == '\\') {
      out_prefix.append("\\\\");
    } else if (*i == '.') {
      out_prefix.append("\\d");
    } else {
      out_prefix.push_back(*i);
    }
  }
  return out_prefix + "." + buf;
}

static string build_admin_prefix(const string &prefix) {
  return prefix + ".admin";
}

static string build_user_prefix(const string &prefix) {
  return prefix + ".user";
}

static string build_missing_prefix(const string &prefix) {
  return prefix + ".removed";
}

struct CloneableAdapter::prefix_status {
  int refs;
  size_t level;
  string ancestor;
  string actual_prefix;
  std::set<string> children;

  void encode(bufferlist &bl) const
  {
    __u8 v = 1;
    ::encode(v, bl);
    ::encode(refs, bl);
    ::encode(level, bl);
    ::encode(ancestor, bl);
    ::encode(actual_prefix, bl);
    ::encode(children, bl);
  }

  void decode(bufferlist::iterator &bl)
  {
    __u8 v;
    ::decode(v, bl);
    ::decode(refs, bl);
    ::decode(level, bl);
    ::decode(ancestor, bl);
    ::decode(actual_prefix, bl);
    ::decode(children, bl);
  }
};

int CloneableAdapter::get_prefix_status(const string &prefix,
					prefix_status *out)
{
  string admin_prefix = build_admin_prefix(prefix);
  std::set<string> keys_to_get;
  keys_to_get.insert(STATUS_KEY);
  map<string, bufferlist> result;
  int r = db->get(admin_prefix, keys_to_get, &result);
  if (r < 0)
    return r;

  if (result.count(STATUS_KEY) != 1)
    return -ENOENT;

  bufferlist::iterator bi = result.begin()->second.begin();
  out->decode(bi);
  return 0;
}

int CloneableAdapter::set_prefix_status(const string &prefix,
					const prefix_status &in)
{
  string admin_prefix = build_admin_prefix(leveled_prefix(prefix, 0));
  map<string, bufferlist> to_set;
  in.encode(to_set[STATUS_KEY]);
  int r = db->set(admin_prefix, to_set);
  return r;
}

int CloneableAdapter::_get(const string &prefix,
			   const std::set<string> &keys,
			   map<string, bufferlist> *out)
{
  prefix_status status;
  map<string, bufferlist> result;
  int r = get_prefix_status(prefix, &status);
  if (r < 0)
    return r;

  r = db->get(build_user_prefix(leveled_prefix(status.actual_prefix, 
					       status.level)), keys, out);
  if (r < 0)
    return r;

  std::set<string> remaining_keys;
  for (std::set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (!out->count(*i))
      remaining_keys.insert(*i);
  }

  if (!remaining_keys.size() || !status.ancestor.size())
    return 0;

  std::set<string> missing_keys;
  r = db->get_keys(build_missing_prefix(
		     leveled_prefix(status.actual_prefix, status.level)),
		   remaining_keys,
		   &missing_keys);
  if (r < 0)
    return r;

  for (std::set<string>::iterator i = missing_keys.begin();
       i != missing_keys.end();
       ++i) {
    remaining_keys.erase(*i);
  }

  if (remaining_keys.size())
    return _get(status.ancestor, remaining_keys, out);
  else
    return 0;

}

int CloneableAdapter::get(const string &prefix,
			  const std::set<string> &keys,
			  map<string, bufferlist> *out)
{
  int r = _get(leveled_prefix(prefix, 0), keys, out);
  if (r == -ENOENT)
    return 0;
  else
    return r;
}

int CloneableAdapter::_get_keys(const string &prefix,
				const std::set<string> &keys,
				std::set<string> *out)
{
  prefix_status status;
  int r = get_prefix_status(prefix, &status);
  if (r < 0)
    return r;

  string lprefix = leveled_prefix(status.actual_prefix, status.level);
  r = db->get_keys(build_user_prefix(lprefix), keys, out);
  if (r < 0)
    return r;

  std::set<string> remaining_keys;
  for (std::set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (!out->count(*i))
      remaining_keys.insert(*i);
  }

  if (!remaining_keys.size() || !status.ancestor.size())
    return 0;

  std::set<string> removed_keys;
  r = db->get_keys(build_missing_prefix(lprefix), remaining_keys, &removed_keys);
  if (r < 0)
    return r;

  for (std::set<string>::iterator i = removed_keys.begin();
       i != removed_keys.end();
       ++i) {
    remaining_keys.erase(*i);
  }

  if (remaining_keys.size())
    return _get_keys(status.ancestor, remaining_keys, out);
  else
    return 0;
}

int CloneableAdapter::get_keys(const string &prefix,
			       const std::set<string> &keys,
			       std::set<string> *out)
{
  int r = _get_keys(leveled_prefix(prefix, 0), keys, out);
  if (r == -ENOENT)
    return 0;
  else
    return r;
}

int CloneableAdapter::set(const string &prefix,
			  const map<string, bufferlist> &to_set) {
  string lprefix = leveled_prefix(prefix, 0);
  prefix_status status;
  int r = get_prefix_status(lprefix, &status);
  if (r == -ENOENT) {
    status.level = 0;
    status.actual_prefix = prefix;
    status.refs = 1;
    r = set_prefix_status(lprefix, status);
    if (r < 0)
      return r;
  } else if (r < 0) {
    return r;
  } else if (status.ancestor.size()) {
    std::set<string> no_longer_removed;
    for (map<string, bufferlist>::const_iterator i = to_set.begin();
	 i != to_set.end();
	 ++i) {
      no_longer_removed.insert(i->first);
      r = db->rmkeys(build_missing_prefix(lprefix), no_longer_removed);
      if (r < 0)
	return r;
    }

  }
  return db->set(build_user_prefix(lprefix), to_set);
}

int CloneableAdapter::rmkeys(const string &prefix,
			    const std::set<string> &keys)
{
  return 0;
}

int CloneableAdapter::rmkeys_by_prefix(const string &prefix)
{
  return 0;
}

int CloneableAdapter::clone(const string &from_prefix, 
			    const string &to_prefix)
{
  return 0;
}

class CloneableAdapterIterator : public KeyValueDB::IteratorInterface {
  CloneableAdapter *parent;
  const string &prefix;
  KeyValueDB::Iterator my_iter, ancestor_iter, missing_iter;
  CloneableAdapter::prefix_status status;
  int r;

public:
  CloneableAdapterIterator(CloneableAdapter *parent,
			   const string &prefix) :
    parent(parent), prefix(prefix), r(0) {}

  int seek_to_first() {
    r = parent->get_prefix_status(prefix, &status);
    if (r < 0)
      return r;

    string lprefix = leveled_prefix(status.actual_prefix, status.level);
    if (!my_iter)
      my_iter = parent->db->get_iterator(build_user_prefix(lprefix));
    r = my_iter->seek_to_first();
    if (r < 0)
      return r;

    if (!ancestor_iter)
      ancestor_iter = parent->_get_iterator(status.ancestor);
    r = ancestor_iter->seek_to_first();
    if (r < 0)
      return r;

    if (!missing_iter)
      missing_iter = parent->db->get_iterator(build_missing_prefix(lprefix));
    r = missing_iter->seek_to_first();
    if (r < 0)
      return r;

    return 0;
  }

  int seek_after(const string &after);
};

KeyValueDB::Iterator CloneableAdapter::_get_iterator(const string &prefix) {
  return Iterator();
}

KeyValueDB::Iterator CloneableAdapter::get_iterator(const string &prefix) {
  return _get_iterator(leveled_prefix(prefix, 0));
}
