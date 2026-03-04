-- WBP_MapEditor - Map Editor Widget Lua Script
-- UnLua binding for WBP_MapEditor Widget Blueprint.
-- Provides height level slider and terrain type combo box controls
-- for the hex grid map editor.

---@type WBP_MapEditor
local WBP_MapEditor = UnLua.Class()

-- Terrain type display names matching ETDTerrainType enum values
-- (excludes MAX sentinel)
local TerrainTypeNames = {
    "Plain",
    "Hill",
    "Mountain",
    "Forest",
    "River",
    "Swamp",
    "DeepWater",
}

-- ---------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------

-- Called by UE when the widget is added to viewport
function WBP_MapEditor:Construct()
    -- Height range constants (ATDHexTile::MinHeightLevel / MaxHeightLevel)
    self.MinHeight = -2
    self.MaxHeight = 3

    -- Current state
    self.SelectedHeightLevel = 0
    self.SelectedTerrainType = "Plain"

    -- Configure HeightSlider
    self:InitHeightSlider()

    -- Configure TerrainType
    self:InitTerrainType()

    -- Bind event callbacks
    self:BindEvents()
end

-- Called by UE when the widget is removed from viewport
function WBP_MapEditor:Destruct()
    self:UnbindEvents()
end

-- ---------------------------------------------------------------
-- Widget Initialization
-- ---------------------------------------------------------------

-- Initialize the height slider to reflect the current SelectedHeightLevel
function WBP_MapEditor:InitHeightSlider()
    if not self.HeightSlider then
        return
    end

    -- Configure slider range and step
    self.HeightSlider:SetMinValue(self.MinHeight)
    self.HeightSlider:SetMaxValue(self.MaxHeight)
    self.HeightSlider:SetStepSize(1)
    self.HeightSlider:SetValue(self.SelectedHeightLevel)
end

-- Populate the terrain type combo box with all terrain type display names
function WBP_MapEditor:InitTerrainType()
    if not self.TerrainType then
        return
    end

    -- Clear any existing options
    self.TerrainType:ClearOptions()

    -- Add each terrain type as an option
    for _, TypeName in ipairs(TerrainTypeNames) do
        self.TerrainType:AddOption(TypeName)
    end

    -- Set default selection
    self.TerrainType:SetSelectedOption(self.SelectedTerrainType)
end

-- ---------------------------------------------------------------
-- Event Binding
-- ---------------------------------------------------------------

-- Bind widget event callbacks
function WBP_MapEditor:BindEvents()
    if self.HeightSlider then
        self.HeightSlider.OnValueChanged:Add(self, self.OnHeightSliderChanged)
    end

    if self.TerrainType then
        self.TerrainType.OnSelectionChanged:Add(self, self.OnTerrainTypeChanged)
    end
end

-- Unbind widget event callbacks
function WBP_MapEditor:UnbindEvents()
    if self.HeightSlider then
        self.HeightSlider.OnValueChanged:Remove(self, self.OnHeightSliderChanged)
    end

    if self.TerrainType then
        self.TerrainType.OnSelectionChanged:Remove(self, self.OnTerrainTypeChanged)
    end
end

-- ---------------------------------------------------------------
-- Event Handlers
-- ---------------------------------------------------------------

-- Called when the height slider value changes.
-- Receives integer height level directly from AnalogSlider [MinHeight, MaxHeight].
---@param Value number  Slider value in [MinHeight, MaxHeight]
function WBP_MapEditor:OnHeightSliderChanged(Value)
    local HeightLevel = math.floor(Value + 0.5)

    -- Clamp to valid range
    HeightLevel = math.max(self.MinHeight, math.min(self.MaxHeight, HeightLevel))

    if HeightLevel ~= self.SelectedHeightLevel then
        self.SelectedHeightLevel = HeightLevel
        self:OnHeightLevelChanged(HeightLevel)
    end
end

-- Called when the terrain type combo box selection changes.
---@param SelectedItem string   The selected terrain type name
---@param SelectionType integer ESelectInfo enum value
function WBP_MapEditor:OnTerrainTypeChanged(SelectedItem, SelectionType)
    local TypeName = tostring(SelectedItem)

    if TypeName ~= self.SelectedTerrainType then
        self.SelectedTerrainType = TypeName
        self:OnTerrainTypeSelectionChanged(TypeName)
    end
end

-- ---------------------------------------------------------------
-- Programmatic Setters
-- ---------------------------------------------------------------

-- Set the height level programmatically, updating both state and slider widget.
---@param NewHeight integer  Height level in [MinHeight, MaxHeight]
function WBP_MapEditor:SetHeightLevel(NewHeight)
    -- Clamp to valid range
    NewHeight = math.max(self.MinHeight, math.min(self.MaxHeight, NewHeight))

    self.SelectedHeightLevel = NewHeight

    -- Update slider position
    if self.HeightSlider then
        self.HeightSlider:SetValue(NewHeight)
    end
end

-- Set the terrain type programmatically, updating both state and combo box widget.
---@param NewType string  Terrain type display name (e.g. "Plain", "Forest")
function WBP_MapEditor:SetTerrainType(NewType)
    self.SelectedTerrainType = NewType

    -- Update combo box selection
    if self.TerrainType then
        self.TerrainType:SetSelectedOption(NewType)
    end
end

-- ---------------------------------------------------------------
-- Getters
-- ---------------------------------------------------------------

-- Get the currently selected height level.
---@return integer  Height level in [-2, 3]
function WBP_MapEditor:GetHeightLevel()
    return self.SelectedHeightLevel
end

-- Get the currently selected terrain type name.
---@return string  Terrain type display name
function WBP_MapEditor:GetTerrainType()
    return self.SelectedTerrainType
end

-- ---------------------------------------------------------------
-- Override Points
-- ---------------------------------------------------------------

-- Called when the height level changes via slider interaction.
-- Override or hook into this for external logic.
---@param NewHeight integer  The new height level
function WBP_MapEditor:OnHeightLevelChanged(NewHeight)
    -- Override point for subclasses or external logic
end

-- Called when the terrain type changes via combo box interaction.
-- Override or hook into this for external logic.
---@param NewType string  The new terrain type display name
function WBP_MapEditor:OnTerrainTypeSelectionChanged(NewType)
    -- Override point for subclasses or external logic
end

return WBP_MapEditor
