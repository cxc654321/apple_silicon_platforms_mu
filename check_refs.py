#!/usr/bin/env python3
"""
L1 静态检查：校验 .dsc / .fdf 引用的 INF 在磁盘上真实存在。

区分三类结果：
  [OK]        文件存在
  [SKIP]      位于未初始化的 Mu / EDK2 子模块内（需要 stuart_update 后才有）
  [WARN]      位于 !if 条件块内（默认构建配置下不会用到）
  [FAIL]      真缺失 —— 会直接导致构建失败

用法（仓库根目录）：python3 tools/check_refs.py
退出码：0 = 无 FAIL，1 = 有 FAIL
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SEARCH_ROOTS = ["", "Platform", "Silicon", "Silicon/Apple", "Common/MU",
                "Common/TIANO", "Common/MU_OEM_SAMPLE", "MU_BASECORE",
                "Silicon/ARM/TIANO", "mu_feature_debugger"]

EXTERNAL_PKGS = {
    "MdePkg", "MdeModulePkg", "ArmPkg", "ArmPlatformPkg", "EmbeddedPkg",
    "FatPkg", "NetworkPkg", "ShellPkg", "UefiCpuPkg", "SecurityPkg",
    "CryptoPkg", "UnitTestFrameworkPkg", "OvmfPkg", "StandaloneMmPkg",
    "PcAtChipsetPkg", "SourceLevelDebugPkg", "FmpDevicePkg", "ArmVirtPkg",
    "DynamicTablesPkg", "UefiPayloadPkg", "IntelFsp2Pkg", "PrmPkg",
    "MsCorePkg", "MsWheaPkg", "MfgPkg", "SmmSupervisorPkg", "AdvLoggerPkg",
    "UefiTestingPkg", "PolicyServicePkg", "SetupDataPkg", "XmlSupportPkg",
    "MmSupervisorPkg", "FeaturePkg", "RedfishPkg", "DebuggerFeaturePkg",
    "DfciPkg", "HidPkg", "MsGraphicsPkg", "OemPkg", "ZeroTouchPkg",
}

SKIP_DIRS = (".git", "Build", "MU_BASECORE", "mu_feature_debugger",
             "Common/MU", "Common/TIANO", "Common/MU_OEM_SAMPLE", "Silicon/ARM/TIANO")

INF_RE = re.compile(r"((?:[A-Za-z0-9_.\-]+/)+[A-Za-z0-9_.\-]+\.inf)")


def resolve(path):
    for root in SEARCH_ROOTS:
        if os.path.isfile(os.path.normpath(os.path.join(REPO, root, path))):
            return True
    return False


def scan(path):
    """返回 [(inf_path, in_conditional)]，自动跳过注释。"""
    out, cond_depth = [], 0
    with open(path, "r", encoding="utf-8", errors="ignore") as fh:
        for raw in fh:
            line = raw
            idx, in_str = None, False
            for i, ch in enumerate(line):
                if ch == '"':
                    in_str = not in_str
                elif ch == "#" and not in_str:
                    idx = i
                    break
            code = line if idx is None else line[:idx]

            s = code.strip()
            if s.startswith("!if"):
                cond_depth += 1
            elif s.startswith("!endif"):
                cond_depth = max(0, cond_depth - 1)
            elif s.startswith("!else"):
                pass

            for m in INF_RE.finditer(code):
                ref = m.group(1)
                if "$" not in ref:
                    out.append((ref, cond_depth > 0))
    return out


def main():
    targets = []
    for dirpath, dirnames, files in os.walk(REPO):
        rel = os.path.relpath(dirpath, REPO)
        if any(rel == s or rel.startswith(s + os.sep) for s in SKIP_DIRS):
            dirnames[:] = []
            continue
        for f in files:
            if f.endswith((".dsc", ".dsc.inc", ".fdf", ".fdf.inc")):
                targets.append(os.path.join(dirpath, f))

    if not targets:
        print("未找到 .dsc/.fdf，请确认在仓库根目录运行")
        return 1

    # 找出没有被任何 .dsc 包含的 .dsc.inc（孤儿文件），其中的缺失只是死引用
    all_text = ""
    for t in targets:
        if t.endswith(".dsc"):
            with open(t, "r", encoding="utf-8", errors="ignore") as fh:
                all_text += fh.read()

    total = ok = skip = 0
    warn, fail = {}, {}
    for t in sorted(targets):
        src = os.path.relpath(t, REPO)
        orphan_inc = src.endswith(".dsc.inc") and os.path.basename(src) not in all_text
        for ref, conditional in scan(t):
            total += 1
            if resolve(ref):
                ok += 1
            elif ref.split("/")[0] in EXTERNAL_PKGS:
                skip += 1
            elif conditional or orphan_inc:
                warn.setdefault(src, set()).add(ref)
            else:
                fail.setdefault(src, set()).add(ref)

    print(f"扫描 {len(targets)} 个 dsc/fdf，共 {total} 条 .inf 引用")
    print(f"  存在 {ok} / 子模块内 {skip} / 条件块 {sum(len(v) for v in warn.values())} "
          f"/ 缺失 {sum(len(v) for v in fail.values())}")

    if warn:
        print("\n[WARN] 引用不存在，但所在文件是条件块或未被任何 .dsc 包含（默认构建不会走到）：")
        for src in sorted(warn):
            for ref in sorted(warn[src]):
                print(f"  {src}: {ref}")

    if fail:
        print("\n[FAIL] 真缺失，会导致构建失败：")
        for src in sorted(fail):
            print(f"  {src}")
            for ref in sorted(fail[src]):
                print(f"     - {ref}")
        return 1

    print("\n[PASS] 无硬缺失")
    return 0


if __name__ == "__main__":
    sys.exit(main())
