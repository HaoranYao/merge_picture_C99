# picture_C99

`picture_C99` 是基于原 `picture` 项目按 C99 重新实现的截图拼接工具，目标是保持 `demo_pic` 下各数据集的处理行为与原项目一致。

## 结构

- `src/`：C99 源码
- `third_party/stb/`：图像解码/编码头文件
- `demo_pic/`：回归样例集
- `tests/`：C99 回归测试

## 构建

项目使用 CMake，生成：

- `picmerge_c99`：命令行程序
- `picmerge_c99_tests`：回归测试

## 用法

```bash
picmerge_c99 <input_dir>
```

行为保持与原项目一致：

- 读取目录中的 `.jpg/.jpeg/.png`
- 跳过 `merge_*.jpg`
- 按自然序排序
- 若目录含子目录，则按子目录逐个独立处理
- 输出 `merge_<timestamp>.jpg`

## 当前说明

本环境里未提供可直接调用的 `cmake`/C 编译器，因此本次已完成工程与源码重写，但尚未在当前机器上完成实际编译执行验证。
