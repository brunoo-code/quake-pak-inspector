return function(path, changed)
    if not changed then return "identical" end
    if path:match("^maps/") then return "map" end
    if path:match("^config/") then return "config" end
    return "asset"
end
