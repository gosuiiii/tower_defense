# 塔防游戏 - GUI分数显示与广告牌结算 测试文档

## 测试日期
2026-04-14

## 测试目标
验证以下功能的正确性：
1. 屏幕左上角GUI显示当前分数（3D像素字体方式）
2. 5关结束后结算画面显示"VICTORY!"和总分数广告牌
3. 失败时显示"GAME OVER"和分数广告牌
4. 烟花庆祝效果

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/main.cpp` | 完整重写，增加3D像素字体系统、ScoreDisplay/VictoryBillboard/GameOverBillboard系统 |

## 新增组件

| 组件 | 类型 | 用途 |
|------|------|------|
| `ScoreDigit` | Tag | 标记分数显示的像素方块实体，用于清理和更新 |
| `Billboard` | Tag | 标记广告牌文字实体 |

## 新增Game字段

| 字段 | 类型 | 用途 |
|------|------|------|
| `billboard_created` | bool | 防止广告牌重复创建 |
| `displayed_score` | int | 记录当前显示的分数，仅分数变化时重建 |

## 新增Scope

| Scope | 用途 |
|-------|------|
| `hud` | HUD元素（分数显示、广告牌）的父级scope |

## 新增系统

| 系统 | 触发条件 | 功能 |
|------|----------|------|
| `ScoreDisplay` | 每帧，score值变化时 | 在地图上方显示"SCORE N"像素文字，金色发光 |
| `VictoryBillboard` | won=true且billboard_created=false | 显示大型"VICTORY!"和"SCORE N"广告牌，带点光源 |
| `GameOverBillboard` | failed=true且billboard_created=false | 显示大型"GAME OVER"和"SCORE N"广告牌，红色 |

## 核心算法：3D像素字体

### 字形定义
- 使用5行×3列的位图定义每个字符
- 每行3位：bit2=左, bit1=中, bit0=右
- 支持字符：A-Z（部分）、0-9、空格、!

### `build_text_3d()` 函数
```
输入: base_pos(起始位置), pixel_size(像素大小), spacing(字间距), text(文本), color(颜色), emissive_val(发光强度), add_billboard(是否添加Billboard标签)
输出: 创建的实体列表
```
- 每个字符占3列×5行的像素方块
- 每个方块带Position、Box、Color、Emissive组件
- 可选添加Billboard标签

### 显示位置
- **HUD分数**: 地图上方偏左 (center.x - 10, y=14, center.z)
  - pixel_size = 0.35, 金色 (1.0, 0.9, 0.3), emissive = 10.0
  - 带跟随点光源
- **胜利广告牌**: 地图上方居中 (center.x, y=12, center.z)
  - "VICTORY!": pixel_size = 0.8, 金色, emissive = 15.0
  - "SCORE N": pixel_size = 1.2, 红橙色, emissive = 20.0
  - 带多个点光源增强效果
- **失败广告牌**: 同上位置
  - "GAME OVER": pixel_size = 0.8, 红色, emissive = 12.0
  - "SCORE N": pixel_size = 1.0, 白色, emissive = 10.0

## 测试用例

### TC-001: 分数HUD显示
**前置条件**: 游戏启动
**测试步骤**:
1. 启动游戏 (`bake run`)
2. 观察地图上方是否出现"SCORE 0"像素文字
3. 等待炮塔消灭怪物
4. 观察分数是否实时更新

**预期结果**:
- 游戏开始时显示"SCORE 0"
- 每消灭一个怪物，分数+1并更新显示
- 分数文字为金色发光效果
- 分数更新时旧文字被清除，新文字重建

### TC-002: 胜利广告牌
**前置条件**: 游戏运行至第5关
**测试步骤**:
1. 持续建造炮塔(B键/L键)确保所有怪物被消灭
2. 通过全部5关
3. 观察结算画面

**预期结果**:
- 屏幕中央上方出现大型"VICTORY!"金色广告牌
- 下方出现更大的"SCORE N"红橙色数字
- 广告牌周围有明亮的点光源
- 同时有烟花粒子效果持续绽放
- 广告牌不会重复创建

### TC-003: 失败广告牌
**前置条件**: 游戏运行中
**测试步骤**:
1. 不建造任何炮塔
2. 等待怪物到达终点

**预期结果**:
- 屏幕中央上方出现大型"GAME OVER"红色广告牌
- 下方显示当前分数
- 红色点光源效果

### TC-004: 分数更新性能
**前置条件**: 游戏运行中
**测试步骤**:
1. 观察多个怪物同时被消灭时的分数更新

**预期结果**:
- 分数更新流畅，无卡顿
- 旧的ScoreDigit实体被正确清理
- 不会累积无效实体

## 编译与运行

### 编译命令
```cmd
cd d:\code\ecs\tower_defense
C:\Users\AAA\bake\bake.bat build
```

### 运行命令
```cmd
cd d:\code\ecs\tower_defense
C:\Users\AAA\bake\bake.bat run
```

### 操作说明
- **B键**: 建造加农炮塔
- **L键**: 建造激光炮塔
- **X键/Delete键**: 删除炮塔

## 技术说明

由于 Flecs Sokol 渲染器不包含2D文字渲染功能（EcsText组件在sokol渲染器中未实现绘制），因此采用3D像素字体方案：
- 用发光的3D小方块拼出文字
- 通过高Emissive值实现醒目的发光效果
- 通过PointLight增强可视性
- HUD分数放在3D世界空间中固定的上方位置
- 广告牌使用更大的像素和更高的发光值以确保醒目

## 已知限制

1. 像素字体为5×3点阵，显示效果较粗糙但风格统一
2. 3D文字不是真正的2D屏幕空间HUD，而是放在世界空间中
3. 分数显示位于地图上方固定位置，相机角度变化时可能遮挡
4. 像素字体仅支持部分大写字母和数字
