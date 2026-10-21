local helper_path = assert(rawget(_G, "helper_path"), "runner must supply helper_path")
local captured, attempts, calls, now, fail, policies
local function run(policy, provider_fail)
  captured = nil
  attempts = 0
  calls = 0
  now = 0
  fail = provider_fail
  policies = {}
  blackbird = {
    binary = {
      encode = function(v)
        if type(v) == "table" and v.requests then
          captured = v
        end
        return "json"
      end,
      decode = function()
        return { id = "check", policy = policy, source = "frozen" }
      end,
    },
    format = function()
      return "native feedback"
    end,
    stats = function()
      return { entries = {}, input_bytes = 10, last_request_bytes = 20 }
    end,
    append = function(items, options)
      for _, i in ipairs(items) do
        if i.role == "developer" then
          -- The arm's policy is an instruction (unit S2), never archived.
          assert(
            options and options.class == "instruction",
            "policy declared instruction"
          )
          policies[#policies + 1] = i.content
        end
      end
    end,
    present = function() end,
    request = function()
      attempts = attempts + 1
      if fail then
        error("unknown provider")
      end
      if attempts == 1 then
        return {
          output = {
            { type = "function_call", name = "probe", arguments = {}, call_id = "id" },
          },
        }
      end
      return { output = { { type = "message" } } }
    end,
    call = function(name, args)
      if name == "read_file" then
        if args.path == "context/pilot-config.bbm" then
          return { content = "config" }
        end
        local f = assert(io.open(args.path))
        local s = f:read("*a")
        f:close()
        return { content = s }
      elseif name == "exec" then
        now = now + 1
        return { exit_code = 0, output = tostring(now) }
      elseif name == "audit_inspect" then
        return { ["end"] = 64, next = 64, records = {} }
      elseif name == "write_file" then
        return { written = true }
      elseif name == "probe" then
        calls = calls + 1
        return { observed = true }
      end
      error("unexpected call " .. name)
    end,
  }
  assert(loadfile(helper_path))()
  assert(captured and captured.source == "frozen" and captured.rrc_seconds == 0)
  assert(captured.requests[1].usage == "unavailable", "missing is not zero")
  if fail then
    assert(
      attempts == 1 and calls == 0 and #captured.unknowns == 1,
      "no unknown provider replay"
    )
  else
    assert(
      attempts == 2 and calls == 1 and captured.finished and #captured.requests == 2
    )
  end
  return policies[1]
end
local a = run("baseline", false)
local b = run("bounded-first", false)
assert(
  a ~= b and a:find("8 requests") and b:find("8 requests"),
  "single policy difference with common envelope"
)
run("baseline", true)
print("finite pilot instrumentation checks passed")
