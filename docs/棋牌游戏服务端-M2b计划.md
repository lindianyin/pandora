# 棋牌游戏服务端 — M2b 实施计划

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 日期 | 2026-09-23 |
| 依据 | [SPEC](./棋牌游戏服务端-SPEC.md) §12 / §14；SRS M2b |
| 前置 | M2 服务端大厅/匹配/斗地主已通 |
| 状态 | **已实现**（game-web 路由化收口） |

---

## 1. 范围

### 包含

- Vue Router：`/login`、`/lobby`、`/table`
- `useGameSession` 共享 WSS 会话（单连不断链换页）
- 大厅：场次刷新、快速匹配、取消、超时提示
- 牌桌：己方在下、倒计时、结算弹层、错误横幅
- 三开窗口：`sessionStorage` 分设备 ID

### 不做（M3+）

- 活动页、钱包/兑换/充值、完整断线重连 `S2C_DdzReconnect`
- protobuf 代码生成（继续手写 `frame.ts`）

---

## 2. 目录

```text
game-web/src/
  router.ts
  composables/useGameSession.ts
  views/LoginView.vue
  views/LobbyView.vue
  views/TableView.vue
  net/GameSocket.ts
  net/frame.ts
```

环境变量（`.env.development`）：

- `VITE_API_BASE=http://127.0.0.1:8080`
- `VITE_WSS_URL=ws://127.0.0.1:8081/`

---

## 3. 验收

1. 打开三个浏览器窗口，各自游客登录进入 `/lobby`
2. 同时快速匹配 → 进入 `/table` → 全员准备
3. 叫分/出牌完成一局，结算弹层可见，金币随 `GetLobby` 刷新
4. 非法出牌出现错误横幅；匹配超时有明确提示
5. `npm run build` 通过；服务端 `m2_smoke.mjs` 仍可用

---

## 4. 启动

```powershell
# 终端1
cd server\build\Release
.\pandora-server.exe

# 终端2
cd game-web
npm install
npm run dev
```
