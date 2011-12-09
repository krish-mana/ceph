// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 

ostream &operator<<(ostream &out, const ContDesc &rhs)
{
  return out << "ObjNum: " << rhs.objnum
	     << " snap: " << rhs.cursnap
	     << " seqnum: " << rhs.seqnum
	     << " prefix: " << rhs.prefix;
}

void VarLenGenerator::get_ranges(const ContDesc &cont, interval_set<uint64_t> &out) {
  RandWrap rand(cont.seqnum);
  uint64_t pos = get_header_length(cont);
  uint64_t limit = get_length(cont);
  out.insert(0, pos);
  bool include = false;
  while (pos < limit) {
    uint64_t segment_length = (rand() % (max_stride_size - min_stride_size)) + min_stride_size;
    assert(segment_length < max_stride_size);
    assert(segment_length >= min_stride_size);
    if (segment_length + pos >= limit) {
      segment_length = limit - pos;
    }
    if (include) {
      out.insert(pos, segment_length);
      include = false;
    } else {
      include = true;
    }
    pos += segment_length;
  }
}

void VarLenGenerator::write_header(const ContDesc &in, bufferlist &output) {
  int data[6];
  data[0] = 0xDEADBEEF;
  data[1] = in.objnum;
  data[2] = in.cursnap;
  data[3] = (int)in.seqnum;
  data[4] = in.prefix.size();
  data[5] = 0xDEADBEEF;
  output.append((char *)data, sizeof(data));
  output.append(in.prefix.c_str(), in.prefix.size());
  output.append((char *)data, sizeof(data[0]));
}

bool VarLenGenerator::read_header(bufferlist::iterator &p, ContDesc &out) {
  try {
    int data[6];
    p.copy(sizeof(data), (char *)data);
    if ((unsigned)data[0] != 0xDEADBEEF || (unsigned)data[5] != 0xDEADBEEF) return false;
    out.objnum = data[1];
    out.cursnap = data[2];
    out.seqnum = (unsigned) data[3];
    int prefix_size = data[4];
    if (prefix_size >= 1000 || prefix_size <= 0) {
      std::cerr << "prefix size is " << prefix_size << std::endl;
      return false;
    }
    char buffer[1000];
    p.copy(prefix_size, buffer);
    buffer[prefix_size] = 0;
    out.prefix = buffer;
    unsigned test;
    p.copy(sizeof(test), (char *)&test);
    if (test != 0xDEADBEEF) return false;
  } catch (ceph::buffer::end_of_buffer e) {
    std::cerr << "end_of_buffer" << endl;
    return false;
  }
  return true;
}

