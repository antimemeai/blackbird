---@meta
---@class BlackbirdContextEntry
---@field id string
---@field item table
---@class BlackbirdContext
---@field base string
---@field entries BlackbirdContextEntry[]
---@class BlackbirdEditOutcome
---@field accepted boolean
---@field revision string
---@field current string
---@field reason string
blackbird = {}
---@param options? table Request assembly overrides, including model/tools/input/reasoning.
---@return table response
function blackbird.request(options) end
---@param name string
---@param arguments table Native arguments; decode external interchange explicitly.
---@param options? {call_id: string} Declares the model function call a relay answers.
---@return any result
function blackbird.call(name, arguments, options) end
---@return BlackbirdContext
function blackbird.context() end
---@param candidate BlackbirdContext
---@return BlackbirdEditOutcome
function blackbird.edit(candidate) end
---@return BlackbirdContextEntry[]
function blackbird.originals() end
---@param id string
---@return BlackbirdContext
function blackbird.restore(id) end
---@param items table[]
---@param options? {class: "instruction"|"result"}
---@return BlackbirdContext
function blackbird.append(items, options) end
---@param text string
function blackbird.display(text) end
---@generic T: table
---@param value T
---@return T
function blackbird.array(value) end
blackbird.json = {}
---@param text string
---@return any
function blackbird.json.decode(text) end
---@param value any
---@return string
function blackbird.json.encode(value) end

---@param item table Completed assistant message; avoids duplicate preview.
function blackbird.present(item) end

---@return boolean Restart scheduled for successful turn boundary.
function blackbird.restarting() end

---@param site integer Positive site scoped to the current source generation.
---@param value number|boolean|string Raw scalar or exact string bytes.
function blackbird.emit(site, value) end

-- Compatibility alias for existing retained scripts.
arco = blackbird

---Owned native BBM2 persistence; accepts raw bytes or a read_file hex byte value.
blackbird.binary = {}
---@param value any
---@return string
function blackbird.binary.encode(value) end
---@param bytes string|table
---@return any
function blackbird.binary.decode(bytes) end
---Bounded native text presentation for operators/model prompts.
---@param value any
---@return string
function blackbird.format(value) end
