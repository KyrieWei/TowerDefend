---
paths:
  - "Source/TowerDefend/UI/**"
---

## 用户需求

实现一套基于MVVM的UI管理框架，基于UnLua实现，目录位于Content/Script目录下

## 产品概述

一个完整的MVVM UI管理框架，为UE5项目提供结构化的UI开发模式。框架将UI逻辑与业务逻辑分离，通过ViewModel作为中间层实现数据绑定和状态管理。

## 核心功能

- **基础MVVM绑定**：实现View-ViewModel-Model三层架构，支持数据双向绑定
- **UI管理器**：提供窗口堆栈管理、层级控制、焦点管理等功能
- **数据绑定系统**：支持属性变更通知、双向数据绑定、显式绑定声明
- **生命周期管理**：完整的Create->Show->Hide->Destroy生命周期控制
- **事件系统**：支持UI事件的统一处理和分发

## 技术栈选择

- **脚本语言**：Lua（基于UnLua 2.3.6）
- **UI系统**：UE5 UMG Widget系统
- **架构模式**：MVVM（Model-View-ViewModel）
- **数据绑定**：显式声明式绑定

## 实现方案

### 架构设计

采用分层架构设计，核心组件包括：

1. **TDUIManager**：UI管理器，负责窗口堆栈、层级管理
2. **TDBaseView**：视图基类，封装UMG Widget操作
3. **TDBaseViewModel**：视图模型基类，提供数据绑定和通知机制
4. **TDBaseModel**：数据模型基类，封装业务数据
5. **TDDataBinding**：数据绑定系统，实现双向绑定
6. **TDEventSystem**：事件系统，处理UI事件分发

### 实现细节

- **数据绑定机制**：通过Lua的元表机制实现属性变更监听，支持get/set拦截
- **UI层级管理**：使用ZOrder和Canvas Panel实现多层级UI堆栈
- **生命周期控制**：定义标准的Create、Show、Hide、Destroy接口
- **内存管理**：实现UI缓存池，避免频繁创建销毁Widget
- **性能优化**：延迟绑定、批量更新、脏标记机制

### 目录结构

```
Content/Script/
├── UI/
│   ├── Framework/
│   │   ├── TDUIManager.lua           # UI管理器
│   │   ├── TDBaseView.lua           # 视图基类
│   │   ├── TDBaseViewModel.lua      # 视图模型基类
│   │   ├── TDBaseModel.lua          # 数据模型基类
│   │   ├── TDDataBinding.lua        # 数据绑定系统
│   │   └── TDEventSystem.lua        # 事件系统
│   ├── Common/
│   │   ├── TDUIDefines.lua          # UI常量定义
│   │   └── TDUIUtils.lua            # UI工具函数
│   └── Examples/
│       ├── TDTestView.lua           # 示例视图
│       ├── TDTestViewModel.lua      # 示例视图模型
│       └── TDTestModel.lua          # 示例数据模型
└── TDUIInit.lua                       # 框架初始化入口
```

### 关键代码结构

#### TDBaseView接口设计

```
-- 视图基类接口
TDBaseView = {
    Create = function(self, widgetClass) end,    -- 创建Widget
    Show = function(self) end,                   -- 显示UI
    Hide = function(self) end,                   -- 隐藏UI
    Destroy = function(self) end,                -- 销毁UI
    BindViewModel = function(self, viewModel) end -- 绑定视图模型
}
```

#### TDBaseViewModel接口设计

```
-- 视图模型基类接口
TDBaseViewModel = {
    Initialize = function(self) end,             -- 初始化
    NotifyPropertyChanged = function(self, propertyName) end, -- 属性变更通知
    BindProperty = function(self, propertyName, callback) end, -- 绑定属性
    SetModel = function(self, model) end         -- 设置数据模型
}
```

#### TDUIManager接口设计

```
-- UI管理器接口
TDUIManager = {
    ShowUI = function(self, uiName, params) end,     -- 显示UI
    HideUI = function(self, uiName) end,             -- 隐藏UI
    GetUI = function(self, uiName) end,              -- 获取UI实例
    SetUILayer = function(self, uiName, layer) end,  -- 设置UI层级
    CloseAllUI = function(self) end                  -- 关闭所有UI
}
```

## Agent Extensions

### SubAgent

- **code-explorer**
- 目的：深入探索UnLua插件的API和现有项目结构，了解UMG Widget的Lua绑定机制
- 预期结果：获得UnLua与UE5 UMG系统集成的详细信息，为框架设计提供技术基础

### MCP

- **unreal-editor**
- 目的：创建示例UMG Widget Blueprint，测试Lua脚本与Widget的交互
- 预期结果：验证框架设计的可行性，确保Lua能够正确操作UMG组件

- **unreal-mcp**
- 目的：创建测试用的Widget Blueprint和相关资产，为框架提供测试环境
- 预期结果：建立完整的测试环境，包括示例Widget和Blueprint资产