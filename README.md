# aspeed2500测试程序

## 1.介绍
用来测试服务器芯片aspeed 2500的底层与应用层代码功能。

-----
## 2.功能测试
### 2.1 p2a工具测试

* 编译工具

```c
# gcc p2a.c -o p2a
```

* 测试读功能
```c
# ./p2a read 0x1e785004
de->d_name: 0000:02:00.0
p2ab_readl: 0x1e785004: 0x00000321
read reg: 0x1e785004, value: 0x00000321
```

* 测试写功能

```c
# ./p2a write 0x1e785004  0x123
de->d_name: 0000:02:00.0
write reg: 0x1e785004, value: 0x00000123
p2ab_readl: 0x1e785004: 0x00000123
read reg: 0x1e785004, value: 0x00000123
```

### 2.2  iLPC2AHB工具测试

* 编译工具

```c
# gcc lpc.c -o lpc
```

* 测试读功能
```c
# # ./lpc  read 0x1e785004
read reg: 0x1e785004, value: 0x00087654
```

* 测试写功能

```c
# # ./lpc  write 0x1e785004  0x123456
write reg: 0x1e785004, value: 0x00123456
read reg: 0x1e785004, value: 0x00123456
```


