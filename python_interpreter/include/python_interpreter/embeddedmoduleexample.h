#pragma once

// 声明 dummy 函数以强制链接
void ensure_fast_calc_linked();

// 该头文件主要用于包含可能需要在此处声明的嵌入模块相关内容，
// 但在这个例子中，模块定义完全在 .cpp 文件中完成。 