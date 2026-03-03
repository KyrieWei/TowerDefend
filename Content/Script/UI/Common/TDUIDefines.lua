-- TowerDefend UI Framework - UI Defines
-- 定义UI框架中使用的常量和枚举

local TDUIDefines = {}

-- UI层级定义
TDUIDefines.UILayer = {
    Background = 0,     -- 背景层
    Normal = 100,       -- 普通UI层
    Popup = 200,        -- 弹窗层
    System = 300,       -- 系统UI层
    Top = 400,          -- 顶层UI
    Debug = 500         -- 调试UI层
}

-- UI状态枚举
TDUIDefines.UIState = {
    None = 0,           -- 未初始化
    Created = 1,        -- 已创建
    Showing = 2,        -- 显示中
    Shown = 3,          -- 已显示
    Hiding = 4,         -- 隐藏中
    Hidden = 5,         -- 已隐藏
    Destroyed = 6       -- 已销毁
}

-- UI动画类型
TDUIDefines.AnimationType = {
    None = 0,           -- 无动画
    Fade = 1,           -- 淡入淡出
    Scale = 2,          -- 缩放
    Slide = 3,          -- 滑动
    Custom = 4          -- 自定义动画
}

-- UI事件类型
TDUIDefines.UIEventType = {
    OnCreate = "OnCreate",
    OnShow = "OnShow", 
    OnHide = "OnHide",
    OnDestroy = "OnDestroy",
    OnFocus = "OnFocus",
    OnLostFocus = "OnLostFocus"
}

-- 数据绑定模式
TDUIDefines.BindingMode = {
    OneWay = 1,         -- 单向绑定（ViewModel -> View）
    TwoWay = 2,         -- 双向绑定
    OneTime = 3         -- 一次性绑定
}

-- 错误码定义
TDUIDefines.ErrorCode = {
    Success = 0,
    UINotFound = 1001,
    UIAlreadyExists = 1002,
    InvalidParameter = 1003,
    WidgetNotFound = 1004,
    ViewModelNotSet = 1005,
    BindingFailed = 1006
}

return TDUIDefines