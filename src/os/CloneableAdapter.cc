// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "CloneableAdapter.h"
#include "include/encoding.h"
#include <map>
#include <set>
#include <string>
using namespace std;

const string STATUS_KEY = "STATUS";

static string translate_prefix(const string &prefix, size_t level) {
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

struct prefix_status {
  int refs;
  size_t level;
  string ancestor;
  string actual_prefix;
  set<string> children;

  void encode(bufferlist &bl) const
  {
    __u8 v = 1;
    ::encode(v, bl);
    ::encode(refs, bl);
    ::encode(level, bl);
    ::encode(ancestor, bl);
    ::encode(actual_prefix, bl);
    ::encode(clone_target, bl);
  }

  void decode(bufferlist::iterator &bl)
  {
    __u8 v;
    ::decode(v, bl);
    ::decode(refs, bl);
    ::decode(level, bl);
    ::decode(ancestor, bl);
    ::decode(actual_prefix, bl);
    ::decode(clone_target, bl);
  }
};

int CloneableAdapter::get_prefix_status(const string &prefix,
					size_t level,
					prefix_status *out)
{
  string admin_prefix = build_admin_prefix(translate_prefix(prefix, level));
  set<string> keys_to_get;
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
  string admin_prefix = build_admin_prefix(prefix, 0);
  map<string, bufferlist> to_set;
  to_set[STATUS_KEY];
  in.encode(to_set[STATUS_KEY]);
  int r = db->set(admin_prefix, to_set);
  return r;
}

int CloneableAdapter::_get(const string &prefix,
			   const set<string> &keys,
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

  set<string> remaining_keys;
  for (set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (!out->count(*i))
      remaining_keys.insert(*i);
  }

  if (!status.ancestor.size())
    return 0;

  set<string> missing_keys;
  r = db->get_keys(build_missing_prefix(
		     leveled_prefix(status.actual_prefix, status.level)),
		   remaining_keys,
		   &mising_keys);
  if (r < 0)
    return r;

  for (set<string>::iterator i = missing_keys.begin();
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
			  const set<string> &keys,
			  map<string, bufferlist> *out)
{
  int r = _get(leveled_prefix(prefix, 0), keys, &found);
  if (r == -ENOENT)
    return 0;
  else
    return r;
}

int CloneableAdapter::_get_keys(const string &prefix,
				const set<string> &keys,
				set<string> *out)
{
  prefix_status status;
  int r = get_status_prefix(prefix, &status);
  if (r < 0)
    return r;

  string lprefix = leveled_prefix(status.actual_prefix, status.level);
  r = db->get_keys(build_user_prefix(lprefix), keys, out);
  if (r < 0)
    return r;

  string remaining_keys;
  for (set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (!out->count(*i))
      remaining_keys.insert(*i);
  }

  if (!status.ancestor.size())
    return 0;

  set<string> removed_keys;
  r = db->get_keys(build_missing_prefix(lprefix), remaining_keys, &removed_keys);
  if (r < 0)
    return r;

  for (set<string>::iterator i = removed_keys.begin();
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
			       const set<string> &keys,
			       set<string> *out)
{
  int r = _get_keys(leveled_prefix(prefix, 0), keys, out);
  if (r == -ENOENT)
    return 0;
  else
    return r;
}

int CloneableAdapter::_get_keys_by_prefix(const string &prefix,
					  size_t max,
					  const string &start,
					  set<string> *out)
{
  prefix_status status;
  int r = get_prefix_status(prefix, &status);
  if (r == -ENOENT)
    return 0;
  else if (r < 0)
    return r;

  string lprefix = leveled_prefix(status.actual_prefix, status.level);
  if (!status.ancestor.size()) {
    return db->get_keys_by_prefix(build_user_prefix(lprefix), max, start, out);
  }

  string ancestor_start = start;
  string my_start = start;
  strint removed_start = start;
  set<string> cur_out;
  set<string> cur_removed;
  while (1) {
    set<string> ancestor_out;
    set<string> my_out;
    set<string> removed;
    // check ancestor first
    if (ancestor_start.size()) {
      r = _get_keys_by_prefix(status.ancestor, max, ancestor_start,
			      &ancestor_out);
      if (r < 0)
	return r;
      if (ancestor_out.size())
	ancestor_start = *(ancestor_out.rbegin());
      else
	ancestor_start = "";
    }

    if (my_start.size()) {
      r = db->get_keys_by_prefix(build_user_prefix(lprefix), max, my_start, 
				 &my_out);
      if (r < 0)
	return r;
      if (my_out.size())
	my_start = *(my_out.rbegin());
      else
	my_start = "";
    }

    if (removed_start.size()) {
      r = db->get_keys_by_prefix(build_missing_prefix(lprefix), max, start,
				 &removed);
      if (r < 0)
	return r;
      if (removed.size())
	removed_start = *(removed->rbegin());
      else
	removed_start = "";
    }

    cur_out.insert(ancestor_start.begin(), ancestor_start.end());
    cur_out.insert(my_start.begin(), my_start.end());
    cur_removed.insert(removed.start(), removed.end());
    while (cur_out.size() &&
	   *(cur_out.begin()) <= ancestor_start &&
	   *(cur_out.begin()) <= my_start) {
      if (!cur_removed.count(*(cur_out.begin())))
	out->insert(*cur_out.begin());
      else {
	while (*removed.begin() != *cur_out.begin())
	  removed.erase(removed.begin());
      }
      cur_removed.erase(cur_out.begin());
      cur_out.erase(cur_out.begin());
    }
  }

  return 0;
}

int CloneableAdapter::get_keys_by_prefix(
  const string &prefix,
  set<string> *out) {
  return _get_by_prefix(leveled_prefix(prefix, 0), out);
}

int CloneableAdapter::_get_by_prefix(const string &prefix,
				     map<string, bufferlist> *out) {
  prefix_status status;
  int r = get_prefix_status(prefix, &status);
  if (r == -ENOENT)
    return 0;
  else if (r < 0)
    return r;

  if (status.ancestor.size()) {
    // check ancestor first
    r = _get_by_prefix(status.ancestor, out);
    if (r < 0)
      return r;

  }

  string lprefix = leveled_prefix(status.actual_prefix, status.level);
  r = db->get_by_prefix(build_user_prefix(lprefix), out);
  if (r < 0)
    return r;
  if (!status.ancestor.size())
    return 0;

  set<string> removed;
  r = db->get_keys_by_prefix(build_missing_prefix(lprefix), &removed);
  if (r < 0)
    return r;

  for (set<string>::iterator i = removed.begin();
       i != removed.end();
       ++i) {
    out->erase(*i);
  }
  return 0;
}

int CloneableAdapter::get_by_prefix(const string &prefix,
				    map<string, bufferlist> *out) {
  return _get_by_prefix(leveled_prefix(prefix, 0), out);
}

int CloneableAdapter::set(string prefix,
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
    set<string> no_longer_removed;
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
			    const set<string> &keys)
}
  return 0;
}

int CloneableAdapter::rmkey_by_prefix(const string &prefix)
{
  return 0;
}

int CloneableAdapter::clone(const string &from_prefix, 
			    const string &to_prefix)
{
  return 0;
}
