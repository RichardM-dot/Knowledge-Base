# src_vector.md —— vector.h 三维向量库

> **对应源文件：`references/firmware/flix-hakimi-pio/src/vector.h`（共 251 行）**
> 全固件几何/数学基础。坐标系约定 **FLU（x 前 y 左 z 上）**，MAVLink 的 FRD 转换只在 mavlink.ino（y/z 取负）。
> 延伸阅读：[src_quaternion.md](src_quaternion.md)（四元数消费 Vector）、[src_estimate.md](src_estimate.md)（rotationVectorBetween 的主用户）、Docs《02-IMU驱动与姿态估计》

## 逐成员表

| 行号 | 成员 | 说明 |
|---|---|---|
| 65 | `x, y, z` | float 分量 |
| 70/75 | 构造函数 | 默认零向量 / 三分量 |
| 78-83 | `zero()` | 严格比较全零（const 只读） |
| 86-99 | `finite/valid/invalid` | NaN 约定与 Vector/Quaternion 一致 |
| 104-108 | `invalidate()` | 全 NaN —— "该级跳过"的载体 |
| 111-113 | `norm()` | sqrt(x²+y²+z²) |
| 118-123 | `normalize()` | **不处理零向量**，调用方保证非零 |
| 127-141 | 标量运算符 | + * / 逐分量 |
| 146-186 | 向量运算符 | + - += -=（Hadamard 逐分量 * /）== != |
| 192-197 | `dot$` | a·b = \|a\|\|b\|cosθ |
| 201-203 | `cross$` | a×b，旋转轴来源（右手） |
| 207-209 | `angleBetween$` | arccos(constrain(点积/模积, ±1))——防浮点出界 NaN |
| 217-235 | `rotationVectorBetween$` | **姿态误差的核心**，见下 |
| 238-246 | `printTo` | Printable 接口 |
| 250-251 | 全局运算符 | 标量在左的 a*v、a+v 复用成员版 |

## 关键摘录：rotationVectorBetween（姿态环误差的来源）

```c
// vector.h:217-235
	static Vector rotationVectorBetween(const Vector& a, const Vector& b) {
		Vector direction = cross(a, b);
		if (direction.zero()) {
			// 叉积为零有两种情形：两向量**同向**（无需旋转）与**反向**（需转 180°）。
			// 原实现把两者都当成"反向"，返回一个未按角度缩放的垂直向量 —— 同向时
			// 会凭空产生一次模长为 |a| 的旋转（acc 的模长是 9.8，×accWeight 后单次
			// 约 1.7° 的姿态冲击）。这里按点积符号区分，同向返回零旋转。
			if (dot(a, b) >= 0) return Vector(0, 0, 0); // 同向或退化：不需要旋转
			// 反向：任取一个与 a 垂直的轴，转过 π（这才是最小旋转的轴角表示）
			Vector axis = cross(a, Vector(1, 0, 0));
			if (axis.zero()) axis = cross(a, Vector(0, 1, 0)); // a 平行 x 轴时换一根
			if (axis.zero()) return Vector(0, 0, 0);           // a 是零向量：无旋转可言
			axis.normalize();
			return axis * PI;
		}
		direction.normalize();
		float angle = angleBetween(a, b);
		return direction * angle;
	}
```

**数学推导**（vector.h:37-50）：`r = θ·n̂ = θ·(a×b)/(|a||b|sinθ)`。两个退化特判：同向返回零旋转；反向绕任意垂直轴（a×x̂，a∥x 时换 ŷ）转 π。

## 使用者地图

| 调用者 | 用法 |
|---|---|
| estimate.applyAcc | `rotationVectorBetween(acc, up) * accWeight` —— 重力修正旋转 |
| estimate.applyLevel | `rotationVectorBetween(Vector(0,0,1), up) * levelWeight` |
| control.controlAttitude | `rotationVectorBetween(upTarget, upActual)` —— 姿态误差（外环输入） |

## 陷阱

- `normalize()` 零向量得 NaN——所有归一化点都先判 `zero()`。
- `angleBetween` 的 constrain 不是数学而是数值防御（浮点让括号内略超 ±1 → arccos NaN）。
- 逐分量 `*`/`/` 是 Hadamard 积，**不是点积/叉积**（用于三轴分别缩放，如加速度计 scale）。

## 关键摘录二：反向退化的轴选择

```c
// vector.h:226-230
			Vector axis = cross(a, Vector(1, 0, 0));
			if (axis.zero()) axis = cross(a, Vector(0, 1, 0)); // a 平行 x 轴时换一根
			if (axis.zero()) return Vector(0, 0, 0);           // a 是零向量：无旋转可言
			axis.normalize();
			return axis * PI;
```

反向（a×b=0 且 a·b<0）时转轴不唯一——任取一根与 a 垂直的轴即可。代码先试 x̂：a∥x 时 a×x̂=0，换 ŷ；两根都零说明 a 本身是零向量（无意义输入，返回零旋转自保）。这条路径在固件里极少走到（需要 up 与 acc 恰好反向 = 倒扣且 landed），但一旦走到，错误实现会产生模长 |a|=9.8 的"旋转"——这就是原 bug 修复的由来（48-50 注释）。

## 数学小抄：本文件承载的三条几何恒等式

| 恒等式 | 在固件里的角色 |
|---|---|
| a·b = \|a\|\|b\|cosθ | angleBetween 反解夹角 → rotationVectorBetween 的模长 |
| a×b = \|a\|\|b\|sinθ·n̂ | 旋转轴方向（右手定则）→ estimate 修正方向 / control 误差方向 |
| (a×b)·c = det[a;b;c] | （未直接用到，但解释了为何 a×b⊥a 且 ⊥b） |

## 与 quaternion.h 的分工边界

Vector 管"方向与角度"（旋转向量 r=θ·n̂ 是它的产物），Quaternion 管"旋转本身"（把旋转向量变成可复合的状态）。转换点只有两处、都在 fromRotationVector / toRotationVector——读代码时看到 `fromRotationVector(rotationVectorBetween(...))` 就知道"算出该转多少、转成四元数去转"。
- `const Vector&` 参数 = 引用传参不拷贝（12 字节），const 表示只读。
