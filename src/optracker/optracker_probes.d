provider ceph_optracker {
  probe op_event(string, string, string);
  probe res_event(struct res_t res, struct op_t op, string event);
}
