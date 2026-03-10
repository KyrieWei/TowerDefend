-- WBP_GameHUD - Game HUD Widget Lua Script
-- UnLua binding for WBP_GameHUD Widget Blueprint.
-- Provides a MapEditorButton to toggle terrain editing mode
-- via the PlayerController's TerrainEditorComponent.

local TDCoreAccessor = require("UI.Helper.TDCoreAccessor")

---@type WBP_GameHUD
local WBP_GameHUD = UnLua.Class()

-- ---------------------------------------------------------------
-- Lifecycle
-- ---------------------------------------------------------------

-- Called by UE when the widget is added to viewport
function WBP_GameHUD:Construct()
    self:BindEvents()
end

-- Called by UE when the widget is removed from viewport
function WBP_GameHUD:Destruct()
    self:UnbindEvents()
end

-- ---------------------------------------------------------------
-- Event Binding
-- ---------------------------------------------------------------

-- Bind widget event callbacks
function WBP_GameHUD:BindEvents()
    if self.MapEditorButton then
        self.MapEditorButton.OnClicked:Add(self, self.OnMapEditorButtonClicked)
    end
end

-- Unbind widget event callbacks
function WBP_GameHUD:UnbindEvents()
    if self.MapEditorButton then
        self.MapEditorButton.OnClicked:Remove(self, self.OnMapEditorButtonClicked)
    end
end

-- ---------------------------------------------------------------
-- Event Handlers
-- ---------------------------------------------------------------

-- Called when the MapEditorButton is clicked.
-- Toggles terrain editing mode via TerrainEditorComponent.
function WBP_GameHUD:OnMapEditorButtonClicked()
    local PlayerController = TDCoreAccessor.GetLocalPlayerController(self)
    if not PlayerController then
        return
    end

    local EditorComp = PlayerController.TerrainEditorComponent
    if not EditorComp then
        return
    end

    EditorComp:ToggleEditMode()
end

return WBP_GameHUD
