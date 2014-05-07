struct res_t {
   string type_id;
   string class_id;
   string inst_id;
};
struct op_t {
   string class_id;
   string inst_id;
};
provider ceph_optracker {
  probe op_event(string, string, string);
  probe res_event(struct res_t res, struct op_t op, string event, string status);
}
