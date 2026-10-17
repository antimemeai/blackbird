---@meta
---Finite presentation constructors in the isolated instrument VM.
ui = {}
---@return integer count
function ui.rows() end
---@param index integer
---@return string key
function ui.key(index) end
---@param index integer
---@return string label
function ui.label(index) end
---@param slot integer One or two; original bounded bytes, including controls/NUL.
---@return string bytes
function ui.detail(slot) end
---@param slot integer
---@return string title
function ui.detail_title(slot) end
---@param key string
---@param title string
---@return integer collection
function ui.collection(key, title) end
---@param collection integer Native constructor index, not execution authority.
---@param key string
---@param label string
---@param input_index integer Copies the actual native input target.
function ui.item(collection, key, label, input_index) end
---@param key string
---@param title string
---@param bytes string Escaped by the native presentation constructor.
function ui.inspector(key, title, bytes) end
---@param verb string Handler phase only. Acceptance is local intent, not delivery.
function ui.request(verb) end
