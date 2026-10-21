-- Model-governed useful-work workflow. Configure paths normally in source.
-- Both source files and boundary steer are retained through audited reads.
local source = blackbird.call(
  "read_file",
  { path = "programs/useful_work.lua", range = { mode = "bytes", start = 0 } }
).content
work = assert(load(source, "useful-work-helpers", "t"))()
local boundary = work.boundary("context/useful-work/steer.bbm")
blackbird.append({
  {
    role = "assistant",
    content = {
      {
        type = "output_text",
        text = "Useful-work boundary: " .. blackbird.format(boundary),
      },
    },
  },
})
for step = 1, 64 do
  local response = blackbird.request()
  local calls = 0
  for _, item in ipairs(response.output) do
    if item.type == "function_call" then
      calls = calls + 1
      local result =
        blackbird.call(item.name, item.arguments, { call_id = item.call_id })
      blackbird.append({
        { type = "function_call_output", call_id = item.call_id, output = result },
      })
    elseif item.type == "message" then
      blackbird.present(item)
    end
  end
  if blackbird.restarting() or calls == 0 then
    return
  end
end
error("Useful-work 64-step budget exhausted; retained work, no automatic replay.")
