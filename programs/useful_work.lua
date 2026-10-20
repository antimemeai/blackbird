-- Editable helpers; load through audited read_file + load(..., ..., 't').
-- No forced compaction, approval gate, replay, or model-output utility judge.
local M = {}
function M.number(v)
  if type(v) == "number" then
    return v
  end
  error("number unavailable, not zero")
end
-- Select exactly one range mode. Actual pilot calls mixing both were rejected.
function M.lines(path, first, last)
  first, last = M.number(first), M.number(last)
  assert(
    first >= 1 and last >= first and first % 1 == 0 and last % 1 == 0,
    "inclusive line range"
  )
  return blackbird.call(
    "read_file",
    { path = path, range = { mode = "lines", start = first, ["end"] = last } }
  )
end
function M.bytes(path, first, last)
  first, last = M.number(first), M.number(last)
  assert(
    first >= 0 and last >= first and first % 1 == 0 and last % 1 == 0,
    "half-open byte range"
  )
  return blackbird.call(
    "read_file",
    { path = path, range = { mode = "bytes", start = first, ["end"] = last } }
  )
end
-- Positional watermark avoids the real reserved-key `end` parser failure.
function M.page(cursor, count, watermark)
  local q = { cursor = cursor, count = count or 32 }
  if watermark ~= nil then
    q["end"] = watermark
  end
  return blackbird.call("audit_inspect", { query = q })
end
function M.original(record, source, offset, limit, watermark)
  local q = { record = record, offset = offset or 0, limit = limit or 4096 }
  if source ~= nil then
    q.source = source
  end
  if watermark ~= nil then
    q["end"] = watermark
  end
  return blackbird.call("audit_inspect", { query = q })
end
-- Bounded search, explicit continuation. Occurrences stay distinct. No replay.
function M.find(cursor, watermark, pages, predicate)
  assert(
    type(predicate) == "function" and pages >= 1 and pages <= 16,
    "declared 1..16-page budget"
  )
  local matches, next_cursor, last = {}, cursor, watermark
  for _ = 1, pages do
    local p = M.page(next_cursor, 64, last)
    last = p["end"]
    next_cursor = p.next
    for _, row in ipairs(p.records) do
      if predicate(row) then
        matches[#matches + 1] = row
      end
    end
    if M.number(next_cursor) >= M.number(last) then
      return { matches = matches, next = next_cursor, watermark = last, complete = true }
    end
  end
  return {
    matches = matches,
    next = next_cursor,
    watermark = last,
    complete = false,
    limitation = "page budget exhausted; unvisited facts unknown",
  }
end
function M.feedback(s)
  s = s or blackbird.stats()
  local entries = {}
  for _, e in ipairs(s.entries or {}) do
    entries[#entries + 1] = { id = e.id, bytes = e.item_bytes }
  end
  table.sort(entries, function(a, b)
    return M.number(a.bytes) > M.number(b.bytes)
  end)
  local largest = {}
  for i = 1, math.min(3, #entries) do
    largest[i] = entries[i]
  end
  return {
    input_bytes = s.input_bytes,
    last_request_bytes = s.last_request_bytes,
    usage = s.usage,
    usage_scope = s.request_scope,
    largest_entries = largest,
    units = "native binary packet bytes, not token estimates; missing usage unavailable",
    choices = {
      "targeted range/search then expand if unresolved",
      "retain concise working map plus original locators",
      "change work sequence/mechanism if repeated inconclusive",
    },
    tradeoff = "Correct useful work and retained constraints first. Shrink is not cheaper native replay or proven uplift.",
  }
end
-- Caller provides a distinct request file per attempt. The immutable audit retains
-- the write and CLI invocation; CLI stores an immutable linked experiment record.
-- Failed/unknown delivery is returned, NEVER retried by these helpers.
function M.record(pool, request_path, r, executable)
  for _, key in ipairs({ "question", "action", "outcome", "references" }) do
    assert(r[key] ~= nil, "missing " .. key)
  end
  assert(request_path:sub(-4) == ".bbm", "native request path must end .bbm")
  r.resources = r.resources or M.feedback()
  local persisted = blackbird.call(
    "write_file",
    { path = request_path, content = blackbird.binary.encode(r) }
  )
  if persisted.written ~= true then
    return { error = "request write not confirmed", write_result = persisted }
  end
  return blackbird.call("exec", {
    argv = {
      executable or "build/release/blackbird-candidate",
      pool,
      "record",
      request_path,
    },
    timeout_seconds = 30,
    output_max_bytes = 4096,
  })
end
function M.contrast(pool, request_path, r, executable)
  for _, key in ipairs({
    "hypothesis",
    "discriminator",
    "baseline",
    "candidate",
    "measurement",
    "disposition",
    "references",
  }) do
    assert(r[key] ~= nil, "missing contrast " .. key)
  end
  assert(
    r.disposition == "keep"
      or r.disposition == "revise"
      or r.disposition == "revert"
      or r.disposition == "inconclusive",
    "invalid disposition"
  )
  assert(
    r.discriminator_declared_before_attempt == true,
    "predeclare discriminator; do not retrofit an oracle"
  )
  return M.record(pool, request_path, {
    question = r.hypothesis,
    action = {
      baseline = r.baseline,
      candidate = r.candidate,
      discriminator = r.discriminator,
    },
    outcome = { measurement = r.measurement, disposition = r.disposition },
    references = r.references,
    resources = r.resources,
  }, executable)
end
-- Persistent steer is source data, applied at the affected workflow boundary.
-- Keep this file local (context/), not a command-approval mechanism. Explicit
-- interrupt is native cancellation. Failed read/parse is visible, not ignored.
function M.boundary(steer_path)
  local raw = blackbird.call(
    "read_file",
    { path = steer_path, range = { mode = "bytes", start = 0 } }
  )
  local steer = blackbird.binary.decode(raw.content)
  assert(
    type(steer) == "table" and type(steer.direction) == "string",
    "invalid operator steer"
  )
  return { steer = steer, feedback = M.feedback() }
end
return M
