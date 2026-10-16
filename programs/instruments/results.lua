-- Composition is pure; only the named handler can request a native intent.
return {
  compose = function()
    local results = ui.collection("results", "RESULTS")
    for i = 1, ui.rows() do
      ui.item(results, ui.key(i), ui.label(i), i)
    end
    ui.inspector(
      "comparison",
      "A / B comparison",
      "A\n"
        .. ui.detail_title(1)
        .. "\n"
        .. ui.detail(1)
        .. "\n\nB\n"
        .. ui.detail_title(2)
        .. "\n"
        .. ui.detail(2)
    )
  end,
  interrupt = function()
    ui.request("interrupt")
  end,
}
