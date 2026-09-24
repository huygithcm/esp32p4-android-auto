# Multi-agent trong du an

Du an duoc cau hinh cho 1 agent dieu phoi va toi da 3 agent phu:

| Vai tro | Cong viec |
|---|---|
| Agent chinh | Chia viec, giao quyen sua file, tich hop, test va ghi log |
| `code_mapper` | Doc code va xac dinh luong xu ly, khong sua file |
| `implementer` | Sua cac file duoc giao va kiem tra thay doi |
| `reviewer` | Review loi va rui ro, khong sua file |

Mo phien Codex moi tai thu muc goc du an. Neu dung PowerShell CLI:

```powershell
codex.cmd
```

Vi du yeu cau:

> Dung multi-agent kiem tra loi gear P/R: code_mapper truy vet luong,
> implementer sua loi theo pham vi duoc giao, reviewer kiem tra ban sua.

Khao sat doc-lap co the chay song song; sua loi phai doi ket qua khao sat
can thiet, review ban sua phai doi implementer. Viec nho khong can mo du 3 agent.

`.codex/config.toml` bat agents va gioi han 3 subagent dong thoi (khong tinh
agent chinh). Cac role trong `.codex/agents/` ke thua model tu phien cha.
Khong thay doi cau hinh Codex toan may hay cau hinh Claude Code.

Client chi nap project config khi du an duoc tin cay. Phien dang mo co the
can khoi dong lai; gioi han cua runtime/tai khoan van duoc uu tien. Neu client
khong ho tro custom role, agent chinh dung vai tro tuong duong qua task prompt
theo AGENTS.md. Cau hinh khong tu khoi chay agent nen khi chua co nhiem vu.

Moi file chi co mot agent sua tai mot thoi diem. Agent chinh ghi log chung;
build dung chung output va truy cap phan cung phai thuc hien tuan tu.

Tham khao: [OpenAI - Subagents](https://developers.openai.com/codex/multi-agent/).
