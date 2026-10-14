#include "blackbird/raw_complaint.hpp"
namespace blackbird {
std::string_view raw_complaint_description() noexcept {
  return R"(Complaint v1: 4500 request parentR,attempt16,invocation16,admitted-generation16;
4501 end requestR,branch:u8,stage:u8,owned-Error-present:bool,[code:u8,detail:i64].
4502 original ownerR,role:u8,size:size_t,unchanged bytes. Roles1 observation,
2 references BBM,3 active attempt hex,4 revision,5 generation hex,6 actor hex,
7 conversation hex,8 workflow hex,9 reporting attempt hex,10 detail BBM,
11 complaint locator,12 title,13 description,14 delivery BBM,15 delivery locator,
16 exit text,17 model,18 effort. No second encoder/formatter or transformed bytes.
4503 predicate ownerR,kind:u8,actual:bool; kind5 also actual active_ids.size:size_t.
Kinds1 observation-empty,2 observation-oversize,3 references-object,4 references-oversize,
5 active truncation,6 exit-present,7 delivered.4504 actual selected unresolved
attempt ownerR,attempt16;4505 formatter operand ownerR,role:u8,identity16;
4506 actual active-set size ownerR,size:size_t;4507 actual audit-end ownerR,count:size_t.
4510 local retain request parentR,detail-originalR;4511 generic end plus returned
locator-originalR only on success.4520 advisory delivery request parentR,complaint-
locatorR,titleR,descriptionR;4521 generic end plus actual delivered:bool on success.
4530 delivery retain request parentR,delivery-originalR;4531 generic end plus returned
locator-originalR only on success. R=lane16,ordinal:u64. Stages1 observation,
2 references,3 active selection,4 detail,5 local retention,6 delivery arguments,
7 nested exec,8 delivered predicate,9 delivery packet,10 delivery retention.
Nested exec retains its actual existing Operation17 parent, not a guessed later
request. Unbound calls remain opt-out. Failed ingestion writes existing8001;
actual raw originals survive later semantic refusal without replay or fabricated locator.)";
}
} // namespace blackbird
