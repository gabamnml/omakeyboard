#include "LuaScanner.h"

#include "Paths.h"
#include "Process.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace omakeyboard {

namespace {
constexpr const char *kLua = "lua";
constexpr const char *kLuac = "luac";
} // namespace

bool LuaScanner::isAvailable()
{
    return Process::isAvailable(QString::fromLatin1(kLua));
}

// The sandbox mirrors the approach in omarchy-menu-keybindings: stub `hl` and
// `o` with permissive no-ops, record what o.bind/hl.bind/hl.unbind receive, and
// print one tab-separated record per line. Reimplemented rather than shelled
// out to so the app does not depend on that script's output format.
QString LuaScanner::sandboxScript()
{
    return QStringLiteral(R"LUA(
local target = os.getenv("OmaKeyboard_CONFIG")
local mode = os.getenv("OmaKeyboard_MODE") or "scan"

local function emit(...)
  if mode == "scan" then
    io.stdout:write(table.concat({ ... }, "\t"), "\n")
  end
end

-- A value that tolerates any indexing or call, so config code that reaches
-- into parts of the API we do not model still runs to completion.
local noop
noop = setmetatable({}, {
  __index = function() return noop end,
  __call = function() return noop end,
  __tostring = function() return "" end,
  __concat = function() return "" end,
  __len = function() return 0 end,
  __add = function() return 0 end,
  __sub = function() return 0 end,
  __mul = function() return 0 end,
  __div = function() return 0 end,
  __unm = function() return 0 end,
  __lt = function() return false end,
  __le = function() return false end,
})

local function literal(value)
  local kind = type(value)
  if kind == "string" then
    return string.format("%q", value)
  elseif kind == "number" or kind == "boolean" then
    return tostring(value)
  elseif kind == "table" then
    local parts = {}
    for index = 1, #value do
      parts[#parts + 1] = literal(value[index])
    end
    local keys = {}
    for key in pairs(value) do
      if not (type(key) == "number" and key >= 1 and key <= #value) then
        keys[#keys + 1] = tostring(key)
      end
    end
    table.sort(keys)
    for _, key in ipairs(keys) do
      parts[#parts + 1] = key .. " = " .. literal(value[key])
    end
    return "{ " .. table.concat(parts, ", ") .. " }"
  end
  return "nil"
end

-- hl.dsp.* calls build a marker table instead of a real dispatcher, so we can
-- tell an exec from any other dispatcher and keep the expression for display.
local function dispatcher(kind, argument)
  return { __omakeyboard = true, kind = kind or "", argument = argument or "" }
end

local function dsp_proxy(path)
  return setmetatable({ path = path }, {
    __index = function(self, key)
      return dsp_proxy(self.path .. "." .. tostring(key))
    end,
    __call = function(self, ...)
      local first = ...
      local parts = {}
      for index = 1, select("#", ...) do
        parts[index] = literal(select(index, ...))
      end
      local expression = self.path .. "(" .. table.concat(parts, ", ") .. ")"
      if self.path == "hl.dsp.exec_cmd" and type(first) == "string" then
        return dispatcher("exec", first)
      end
      return dispatcher("lua", expression)
    end,
  })
end

local function record_bind(keys, target_dispatcher, opts)
  opts = opts or {}
  local kind, argument = "", ""

  if type(target_dispatcher) == "table" and target_dispatcher.__omakeyboard then
    kind = target_dispatcher.kind
    argument = target_dispatcher.argument
  elseif type(target_dispatcher) == "string" then
    kind, argument = "exec", target_dispatcher
  elseif type(target_dispatcher) == "function" then
    kind = "lua"
  end

  emit("BIND", tostring(keys or ""), tostring(opts.description or opts.desc or ""),
       kind, argument)
end

hl = setmetatable({
  dsp = dsp_proxy("hl.dsp"),
  bind = function(keys, target_dispatcher, opts)
    record_bind(keys, target_dispatcher, opts)
    return noop
  end,
  unbind = function(keys)
    emit("UNBIND", tostring(keys or ""))
    return noop
  end,
  -- get_config must answer with something arithmetic-safe: real configs do
  -- `(hl.get_config("cursor.zoom_factor") or 1) + 1`.
  get_config = function() return nil end,
  get_current_submap = function() return "" end,
  version = function() return "0.0.0" end,
  config = function() return noop end,
  device = function() return noop end,
  on = function() return noop end,
  timer = function() return noop end,
}, {
  __index = function() return noop end,
})

-- Omarchy's helpers.lua defines `o` on top of hl, so loading the real file
-- keeps o.bind's dispatcher vocabulary ({ omarchy = }, { launch = }, ...)
-- exactly as Omarchy defines it instead of reimplementing it here.
o = o or {}

local ok, err = pcall(function()
  dofile(target)
end)

if not ok then
  io.stderr:write("ERROR\t", tostring(err), "\n")
  os.exit(1)
end

os.exit(0)
)LUA");
}

ScanResult LuaScanner::scanConfig() const
{
    return scanConfigFile(Paths::hyprlandLuaFile());
}

ScanResult LuaScanner::scanConfigFile(const QString &path) const
{
    ScanResult result;

    if (!isAvailable()) {
        result.error = QStringLiteral("lua is not installed, so existing bindings "
                                      "cannot be read.");
        return result;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OmaKeyboard_CONFIG"), path);
    environment.insert(QStringLiteral("OmaKeyboard_MODE"), QStringLiteral("scan"));

    // The script arrives on stdin so nothing has to be written to disk.
    QProcess process;
    process.setProgram(QString::fromLatin1(kLua));
    process.setArguments({ QStringLiteral("-") });
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(3000)) {
        result.error = process.errorString();
        return result;
    }
    process.write(sandboxScript().toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("The configuration scan timed out.");
        return result;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString errors = QString::fromUtf8(process.readAllStandardError());

    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.isEmpty())
            continue;

        if (fields.at(0) == QStringLiteral("BIND") && fields.size() >= 3) {
            ScannedBind bind;
            bind.keys = fields.value(1);
            bind.description = fields.value(2);
            bind.kind = fields.value(3);
            bind.argument = fields.value(4);
            if (!bind.keys.isEmpty())
                result.binds.append(bind);
        } else if (fields.at(0) == QStringLiteral("UNBIND") && fields.size() >= 2) {
            const QString keys = fields.value(1);
            if (!keys.isEmpty() && !result.unbinds.contains(keys))
                result.unbinds.append(keys);
        }
    }

    if (process.exitCode() != 0) {
        result.error = errors.trimmed().isEmpty()
            ? QStringLiteral("The configuration could not be evaluated.")
            : errors.trimmed();
        return result;
    }

    result.ok = true;
    return result;
}

bool LuaScanner::checkSyntax(const QString &path, QString *error) const
{
    if (!Process::isAvailable(QString::fromLatin1(kLuac))) {
        // Fall back to loading the chunk without running it, which is the same
        // check luac -p performs.
        const ProcessResult result = Process::run(
            QString::fromLatin1(kLua),
            { QStringLiteral("-e"),
              QStringLiteral("local f, e = loadfile(...); if not f then io.stderr:write(e) "
                             "os.exit(1) end"),
              path },
            4000);
        if (result.ok())
            return true;
        if (error)
            *error = result.stdErr.trimmed();
        return false;
    }

    const ProcessResult result = Process::run(
        QString::fromLatin1(kLuac),
        { QStringLiteral("-p"), path }, 4000);
    if (result.ok())
        return true;
    if (error)
        *error = result.stdErr.trimmed().isEmpty()
            ? QStringLiteral("%1 has a syntax error").arg(path)
            : result.stdErr.trimmed();
    return false;
}

bool LuaScanner::dryRun(const QString &configPath, QString *error) const
{
    const ScanResult result = scanConfigFile(configPath);
    if (result.ok)
        return true;
    if (error)
        *error = result.error;
    return false;
}

} // namespace omakeyboard
