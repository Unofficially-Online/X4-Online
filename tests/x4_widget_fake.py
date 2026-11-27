"""A stand-in for X4 9.00's widget handles as the Multiplayer pages use them (ui/addons/ego_detailmonitorhelper's
frame, table, row and cell handles, Helper's sizes, the Color lookup), for tests under LuaJIT (lupa). It is written from
what those handles accept, not from their code: every property a page sets must be one X4 knows for that widget type
(X4 logs any other as a setup error), a colspan may not run past the table or over a created cell, a cell is created
once, and column widths are set before the first row. Breaches go to widget_errors; the cells drawn go to `cells`."""

WIDGETS = r"""
widget_errors, cells, frames = {}, {}, {}
local function fail(text) widget_errors[#widget_errors + 1] = text end
local function set_of(...) local s = {} for _, list in ipairs({...}) do for k in list:gmatch('%S+') do s[k] = true end end return s end
local WIDGET = 'scaling width height x y mouseOverText helpOverlayText helpOverlayID helpOverlayX helpOverlayY helpOverlayWidth '
  .. 'helpOverlayHeight helpOverlayHighlightOnly helpOverlayScaling helpOverlayUseBackgroundSpan'
local CELL = WIDGET .. ' cellBGColor uiTriggerID'
ALLOWED = {
  text = set_of(CELL, 'text halign color glowfactor titleColor font fontsize wordwrap x y minRowHeight'),
  icon = set_of(CELL, 'icon color glowfactor affectRowHeight'),
  button = set_of(CELL, 'active bgColor highlightColor borderColor height affectRowHeight'),
  editbox = set_of(CELL, 'bgColor closeMenuOnBack defaultText description textHidden encrypted selectTextOnActivation active '
    .. 'restoreInteractiveObject maxChars'),
  dropdown = set_of(CELL, 'options startOption active bgColor bg2Color highlightColor borderColor optionColor optionWidth '
    .. 'optionHeight background2Width background2Height background2X background2Y allowMouseOverInteraction textOverride '
    .. 'text2Override text3Override text4Override rightSideArrow'),
  statusbar = set_of(CELL, 'current start max valueColor posChangeColor negChangeColor markerColor titleColor'),
  textproperty = set_of('text x y halign color glowfactor font fontsize scaling'),
  iconproperty = set_of('icon swapicon width height x y color glowfactor anchorID scaling'),
  row = set_of('scaling fixed borderBelow interactive bgColor multiSelected paddingTop paddingBottom'),
  table = set_of(WIDGET, 'frameborder header tabOrder skipTabChange defaultInteractiveObject borderEnabled maxVisibleHeight '
    .. 'reserveScrollBar wraparound highlightMode multiSelect backgroundID backgroundColor backgroundPadding prevTable nextTable '
    .. 'prevHorizontalTable nextHorizontalTable'),
}
local function strict(kind, where, properties)
  for key in pairs(properties or {}) do
    if not ALLOWED[kind][key] then fail(where .. ': no property ' .. kind .. '.' .. tostring(key)) end
  end
end
-- a properties table that checks every later write (as X4's metatables do)
local function checked(kind, where, initial)
  strict(kind, where, initial)
  local raw = {}
  for k, v in pairs(initial or {}) do raw[k] = v end
  return setmetatable({}, {__index = raw, __newindex = function(_, key, value)
    if not ALLOWED[kind][key] then fail(where .. ': set no property ' .. kind .. '.' .. tostring(key)) end
    raw[key] = value
  end, raw = raw})
end
function props_of(p) return getmetatable(p).raw end

Helper = {borderSize = 3, viewWidth = 1920, viewHeight = 1080}
function Helper.scaleX(v) return v end
function Helper.scaleY(v) return v end
-- X4's Color: a lookup that logs a name it does not have
Color = setmetatable({}, {__index = function(_, name) fail('no colour ' .. tostring(name)) return {r = 255, g = 0, b = 255, a = 60} end})
for name in ('text_normal text_lowlight text_inactive text_success text_positive text_warning text_error text_inprogress '
  .. 'text_player_current icon_normal icon_positive icon_warning icon_error row_title row_separator row_background '
  .. 'row_background_unselectable statusbar_value_default statusbar_marker_hidden optionsmenu_cell_background '
  .. 'frame_background_notification'):gmatch('%S+') do rawset(Color, name, {name = name}) end

local function new_cell(row, index)
  local cell = {row = row, index = index, colspan = 1, type = 'cell', handlers = {}}
  cell.properties = checked('text', 'cell', {})   -- replaced at creation
  local function create(kind, properties)
    if cell.colspan == 0 then fail(string.format('cell [%d][%d] created under a colspan', row.index, index)) end
    -- X4's view check: 'Button defined in an unselectable row' (row data nil or false), the same for the other inputs
    if (kind == 'button' or kind == 'editbox' or kind == 'dropdown') and not row.rowdata then
      fail(string.format('%s [%d][%d] in an unselectable row', kind, row.index, index))
    end
    if cell.type ~= 'cell' then fail(string.format('cell [%d][%d] created twice', row.index, index)) end
    cell.type = kind
    cell.properties = checked(kind, kind .. ' [' .. row.index .. '][' .. index .. ']', properties)
    cells[#cells + 1] = cell
  end
  -- as X4's: a colspan hides the cells it covers; a shorter one gives back the cells the longer one hid
  function cell:setColSpan(n)
    if n < 1 or self.index + n - 1 > row.table.numcolumns then fail('colspan ' .. n .. ' at column ' .. self.index .. ' of ' .. row.table.numcolumns) end
    if self.colspan == 0 then fail('colspan on the hidden cell [' .. row.index .. '][' .. self.index .. ']') end
    local before = self.colspan
    for i = 1, n - 1 do
      local other = row[self.index + i]
      if other and other.type ~= 'cell' then fail('colspan hides the created cell ' .. other.index) end
      if other then before = before + other.colspan other.colspan = 0 end
    end
    for i = n, before - 1 do
      if row[self.index + i] then row[self.index + i].colspan = 1 end
    end
    self.colspan = n
    return self
  end
  function cell:getColSpanWidth() return 100 * self.colspan end
  function cell:getWidth() return 100 end
  function cell:createText(text, properties) create('text', properties) self.text = text return self end
  function cell:createIcon(icon, properties)
    if icon == nil or icon == '' then fail('empty icon') end
    create('icon', properties) self.icon = icon return self
  end
  function cell:createButton(properties) create('button', properties) return self end
  function cell:setText(text, properties)
    if self.type == 'button' then strict('textproperty', 'button text', properties) self.label = text
    elseif self.type == 'editbox' then strict('textproperty', 'editbox text', properties) self.boxtext = text
    else fail('setText on a ' .. self.type) end
    return self
  end
  function cell:setIcon(icon, properties) strict('iconproperty', 'button icon', properties) self.icon = icon return self end
  function cell:createEditBox(properties) create('editbox', properties) return self end
  function cell:createDropDown(options, properties) create('dropdown', properties) self.options = options return self end
  function cell:setTextProperties(properties) strict('textproperty', 'dropdown text', properties) return self end
  function cell:createStatusBar(properties) create('statusbar', properties) return self end
  return cell
end

function new_frame(width)
  local frame = {tables = {}, properties = {width = width or 800}}
  frames[#frames + 1] = frame
  function frame:addTable(numcols, properties)
    local t = {frame = frame, numcolumns = numcols, rows = {}, index = #frame.tables + 1, widths = {}}
    t.properties = checked('table', 'table ' .. t.index, properties)
    frame.tables[#frame.tables + 1] = t
    local function width_set(col)
      if #t.rows > 0 then fail('column width set after the first row of table ' .. t.index) end
      if col < 1 or col > numcols then fail('column ' .. col .. ' of ' .. numcols) end
    end
    function t:setColWidth(col, w, scaling) width_set(col) t.widths[col] = w end
    function t:setColWidthPercent(col, w) width_set(col) t.widths[col] = w .. '%' end
    function t:setDefaultCellProperties(kind, p) strict(kind, 'default ' .. kind, p) end
    function t:setDefaultComplexCellProperties(kind, complex, p) strict('textproperty', 'default ' .. kind .. '.' .. complex, p) end
    t.default_colspans = {}
    function t:setDefaultColSpan(col, n) t.default_colspans[col] = n end
    function t:addRow(rowdata, properties)
      local row = {table = t, index = #t.rows + 1, rowdata = rowdata}
      row.properties = checked('row', 'row ' .. row.index, properties)
      for i = 1, numcols do row[i] = new_cell(row, i) end
      for i = numcols, 1, -1 do   -- the default colspans, as X4 applies them to every new row
        if (t.default_colspans[i] or 1) > 1 then row[i]:setColSpan(t.default_colspans[i]) end
      end
      t.rows[#t.rows + 1] = row
      return row
    end
    function t:addEmptyRow(height) return t:addRow(false, {}) end
    function t:getVisibleHeight() return 20 * #t.rows end
    function t:getFullHeight() return 20 * #t.rows end
    function t:setSelectedRow(index) t.selected = index end
    function t:setTopRow(index) t.top = index end
    return t
  end
  function frame:display() frame.shown = true end
  return frame
end
"""

# X4's stock gameoptions.lua as far as the page uses it, with its frame: the page's options frame, the 5-column table
# with the title row, the stock row function (it records the options it draws, for the fallback rows).
MENU = r"""
menu = {name = 'OptionsMenu', isStartmenu = true, refreshes = 0, widthExtraWide = 1220,
  table = {x = 45, y = 45, width = 710, widthExtraWide = 1130, widthWithExtraInfo = 370, height = 980, arrowColumnWidth = 20,
    infoColumnWidth = 330}}
function menu.displayOption(ftable, option, numCols) drawn[#drawn + 1] = option end
function menu.createOptionsFrame(extrawide)
  menu.optionsFrame = new_frame(extrawide and 1220 or 800)
  return menu.optionsFrame
end
function menu.displayOptions(p)
  menu.currentOption = p
  drawn = {}
  local frame = menu.createOptionsFrame()
  local ftable = frame:addTable(5, {tabOrder = 1, x = 45, y = 45, width = 710, maxVisibleHeight = 980})
  ftable:setColWidth(1, 20, false)
  ftable:setColWidth(3, 165, false)
  ftable:setColWidth(4, 145, false)
  ftable:setColWidth(5, 16, false)
  ftable:setDefaultColSpan(3, 3)   -- the value cells span the value columns, as X4's options page
  local row = ftable:addRow(true, {fixed = true})
  row[1]:createButton({height = 34}):setIcon('table_arrow_inv_left', {x = 3})
  row[2]:setColSpan(4):createText(config.optionDefinitions[p].name, {fontsize = 13})
  for _, o in ipairs(config.optionDefinitions[p]) do menu.displayOption(ftable, o) end
  frame:display()
end
function menu.submenuHandler(p) if config.optionDefinitions[p] then menu.displayOptions(p) end end
function menu.refresh() menu.refreshes = menu.refreshes + 1 menu.submenuHandler(menu.currentOption) end
Menus = {{name = 'MapMenu'}, menu}
"""


def lua_list(table):
    """A Lua array as a Python list."""
    return [table[i] for i in range(1, len(table) + 1)] if table is not None else []


def value(lua, item):
    """A cell's text: its function called (X4 reads function cells every frame)."""
    if lua.eval('type')(item) == 'function':
        return item()
    return item


class Drawn:
    """The cells of the last draw, by table: texts, buttons, and the actions behind them."""

    def __init__(self, lua, g):
        self.lua, self.g = lua, g
        frame = lua_list(g.frames)[-1]
        self.frame = frame
        self.tables = lua_list(frame.tables)

    def cells(self, table_index=None):
        out = []
        for index, table in enumerate(self.tables, 1):
            if table_index is not None and index != table_index:
                continue
            for row in lua_list(table.rows):
                for col in range(1, table.numcolumns + 1):
                    cell = row[col]
                    if cell.type != 'cell':
                        out.append(cell)
        return out

    def texts(self, table_index=None):
        return [str(value(self.lua, c.text)) for c in self.cells(table_index) if c.type == 'text']

    def buttons(self, table_index=None):
        return {str(value(self.lua, c.label)): c for c in self.cells(table_index) if c.type == 'button'}

    def button(self, label, table_index=None):
        return self.buttons(table_index)[label]

    def press(self, label, table_index=None):
        self.button(label, table_index).handlers.onClick()

    def active(self, label, table_index=None):
        return bool(self.g.props_of(self.button(label, table_index).properties).active)

    def tips(self):
        return [str(self.g.props_of(c.properties).mouseOverText or '') for c in self.cells()]

    def of_type(self, kind):
        return [c for c in self.cells() if c.type == kind]
