# 参与贡献 {#getting-involved}

## 建议功能 {#suggesting-a-feature}

如果你认为 TrenchBroom 缺少某项功能，可以在 [TrenchBroom issue tracker] 提交功能请求。请尽量描述清楚你的想法，但不必一开始就写得过于详细。如果该功能被采纳，我们会一起完善具体方案。

## 报告错误 {#reporting_bugs}

你可以在 [TrenchBroom issue tracker] 提交错误报告。请务必包含以下信息：

- *TrenchBroom 版本*：例如“Version 2.0.0 f335082 D”（见下文）。
- *操作系统及版本*：例如“Windows 7 64-bit”。
- *崩溃报告和地图文件*：TrenchBroom 崩溃时会自动保存崩溃报告和地图文件。这些文件会放在当前地图文件所在的文件夹中；如果当前地图尚未保存，则放在文档文件夹中。例如，正在编辑的地图文件名为“rtz_q1.map”时，崩溃报告名为“rtz_q1-crash.txt”，保存的地图文件名为“rtz_q1-crash.map”。已有文件不会被覆盖，TrenchBroom 会在文件名末尾添加编号来创建新文件。报告错误时，请选择编号最大的文件。
- *准确的复现步骤*：请尽可能提供能够准确复现问题的信息。有时问题很难用文字描述，此时可以附加截图或录屏。如果你无法稳定复现问题，也请提交报告，因为我们通常仍然可以推断出原因。

## 参与开发 {#contributing}

更多详情请参阅 [TrenchBroom CONTRIBUTING.md] 文件。

### 版本信息 {#the-version-information}

从菜单打开“关于 TrenchBroom”对话框。左侧的浅灰色文字会显示当前运行的 TrenchBroom 版本，例如“Version 2.0.0 f335082 D”。前三个数字表示版本号（2.0.0），后面的七个字符是构建 ID（f335082），最后一个字母表示构建类型（“D”表示 Debug，“R”表示 Release）。启动时显示的欢迎窗口中也可以找到这些信息。

*点击版本信息文字即可将其复制到剪贴板，这对提交错误报告很有帮助。*

## 联系方式 {#contact}

- [TrenchBroom Discord]

# 参考与链接 {#references_and_links}

- [TrenchBroom on GitHub] - TrenchBroom 的 GitHub 页面
- [func_msgboard] - Quake 地图制作论坛
- [Quake Tools] - Joshua Skelton 制作的 Quake 工具
- [Tutorials by dumptruck_ds] - 视频教程系列
- [Quake Level Design Starter Kit] - 开箱即用的入门套件
- [Quake Mapping Discord] - Quake 地图制作 Discord 社区
- [Tome of Preach] - Quake 地图和 QuakeC 技巧

[TrenchBroom on GitHub]: https://github.com/TrenchBroom/TrenchBroom/
[TrenchBroom issue tracker]: https://github.com/TrenchBroom/TrenchBroom/issues/
[TrenchBroom CONTRIBUTING.md]: https://github.com/TrenchBroom/TrenchBroom/blob/master/CONTRIBUTING.md
[TrenchBroom Discord]: https://discord.gg/WGf9uve
[func_msgboard]: https://celephais.net/board/
[Quake Tools]: https://joshua.itch.io/quake-tools
[Tome of Preach]: https://tomeofpreach.wordpress.com/
[FGD File Format]: https://developer.valvesoftware.com/wiki/FGD
[Tutorials by dumptruck_ds]: https://www.youtube.com/playlist?list=PLgDKRPte5Y0AZ_K_PZbWbgBAEt5xf74aE
[Quake Level Design Starter Kit]: https://github.com/jonathanlinat/quake-leveldesign-starterkit
[Quake Mapping Discord]: https://discordapp.com/invite/f5Y99aM
[FreeImage Library]: https://freeimage.sourceforge.io/
