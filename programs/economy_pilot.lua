-- Finite matched useful-work pilot. Config and raw records remain local.
-- Same instrumentation and context policy in both arms; one information policy differs.
local cfg = blackbird.binary.decode(
  blackbird.call(
    "read_file",
    { path = "context/pilot-config.bbm", range = { mode = "bytes", start = 0 } }
  ).content
)
local M = assert(
  load(
    blackbird.call(
      "read_file",
      { path = "programs/useful_work.lua", range = { mode = "bytes", start = 0 } }
    ).content,
    "@programs/useful_work.lua",
    "t"
  )
)()
assert(cfg.policy == "baseline" or cfg.policy == "bounded-first")
local policies = {
  baseline = "Use the ordinary coding/inspection workflow. You may use all allotted attempts to inspect source and refine your result.",
  ["bounded-first"] = "Gather information bounded-first: targeted searches and relevant ranges, expanding when the immediate question remains unresolved. Retain a concise working map with original/source locators. Do not omit constraints, failures or checks for economy.",
}
blackbird.append({
  {
    role = "developer",
    content = policies[cfg.policy]
      .. " Common rules: only inspect declared project sources and write the declared local deliverable. No delegates, network commands, checkout changes, background programs, builds, restart or context compaction. Same access and up to 8 requests in either arm. No command approvals. Feedback is advisory; correctness comes first.",
  },
  { role = "assistant", content = blackbird.format(M.feedback()) },
})
local records = {
  id = cfg.id,
  policy = cfg.policy,
  source = cfg.source,
  config = cfg,
  requests = {},
  clock = "wall seconds via audited date; resolution1s; not CPU or monotonic",
  replay_latency = "unavailable",
  rrc_seconds = 0,
  build_seconds = 0,
  colleagues = "none permitted",
  unknowns = {},
}
local function clock()
  local r = blackbird.call(
    "exec",
    { argv = { "date", "+%s" }, timeout_seconds = 5, output_max_bytes = 128 }
  )
  assert(M.number(r.exit_code) == 0, "clock failure")
  return assert(tonumber(r.output))
end
local start = clock()
for step = 1, 8 do
  local a = clock()
  local ok, response = pcall(blackbird.request)
  local b = clock()
  local s = assert(blackbird.stats(), "stats unavailable")
  local rec = {
    step = step,
    inference_wall_seconds = b - a,
    last_request_bytes = s.last_request_bytes,
    usage = s.usage or "unavailable",
    tools = {},
  }
  records.requests[#records.requests + 1] = rec
  local p = M.page(0, 1)
  local finish = M.number(p["end"])
  rec.audit = M.page(math.max(0, finish - 64), 64, p["end"])
  if not ok then
    rec.usage = "unavailable"
    records.unknowns[#records.unknowns + 1] =
      { phase = "provider", detail = tostring(response), usage = "unavailable" }
    break
  end
  local calls = 0
  for _, item in ipairs(response.output) do
    if item.type == "function_call" then
      calls = calls + 1
      local t = clock()
      local success, result = pcall(blackbird.call, item.name, item.arguments)
      local u = clock()
      rec.tools[#rec.tools + 1] =
        { name = item.name, wall_seconds = u - t, completed = success }
      if not success then
        result = { error = tostring(result), effect_outcome = "unknown" }
        records.unknowns[#records.unknowns + 1] = { phase = "tool", name = item.name }
      end
      blackbird.append({
        { type = "function_call_output", call_id = item.call_id, output = result },
      })
    elseif item.type == "message" then
      blackbird.present(item)
    end
  end
  if calls == 0 then
    records.finished = true
    break
  end
end
records.budget_exhausted = not records.finished and #records.unknowns == 0
records.elapsed_instrumented_seconds = clock() - start
local w = blackbird.call(
  "write_file",
  { path = "context/pilot-metrics.bbm", content = blackbird.binary.encode(records) }
)
assert(w.written, "metrics write unconfirmed")
