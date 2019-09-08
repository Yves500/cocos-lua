local class         = require "xgame.class"
local Event         = require "xgame.event.Event"
local MixFGUI       = require "xgame.ui.MixFGUI"
local ui            = require "xgame.ui.ui"
local UIPackage     = require "fgui.UIPackage"
local UIEventType   = require "fgui.UIEventType"
local FileBrowser   = require "system.console.FileBrowser"
local LogCat        = require "system.console.LogCat"

local Console = class("Console", MixFGUI)

function Console:ctor()
end

function Console:onCreate()
    UIPackage.addPackage("res/ui/console")

    self.view = UIPackage.createObject("console", "main")
    self.view.width = self.width
    self.view.height = self.height
    self.rootfgui:addChild(self.view)
    self.rootfgui:setSize(self.width, self.height)

    local tabController = self.view:resolve('panel#tab')
    tabController:addEventListener(UIEventType.Changed, function ()
        local prev = self.tab[tabController.previousPage]
        local curr = self.tab[tabController.selectedPage]
        if prev then
            prev:stop()
        end
        if curr then
            curr:start()
        end
    end)

    local logcat = LogCat.new(self)
    logcat:start()

    local fileBrowser = FileBrowser.new(self, 'https://codetypes.com/testswf/')
    fileBrowser:addListener(Event.SELECT, function (_, data)
        if string.find(data.url, '%.swf$') then
            self.visible = false
            self.rootfgui.visible = false
            self.cobj:onExit()
            xGame:startScene('system.console.TestSWF', data.url, function ()
                self.visible = true
                self.rootfgui.visible = true
                self.cobj:onEnter()
            end)
        end
    end)

    self.tab = {
        SWF = fileBrowser,
        LOG = logcat,
    }

    -- init toggle button
    local btnOpen = self:addChild(ui.inflate {
        classname = "UILayer",
        horizontalAlign = "left",
        verticalAlign = "bottom",
        width = 80,
        height = 80,
        touchable = true,
        touchChildren = false,
        -- colorType = 1,
        -- color = 0x999999,
    })
    local function click ()
        if self.rootfgui.visible then
            self.rootfgui.visible = false
            self.cobj:onExit()
            btnOpen.cobj:onEnter()
        else
            self.rootfgui.visible = true
            self.cobj:onEnter()
        end
    end
    btnOpen.cobj.touchEnabled = true
    btnOpen.cobj:addClickEventListener(click)
    click() -- hide console panel
end

return Console