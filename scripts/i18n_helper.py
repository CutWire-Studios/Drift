#!/usr/bin/env python3
"""
i18n_helper.py - Tooling to rank, prepare, extract, apply, and finalize Qt Linguist (.ts) translation files.
"""

import os
import sys
import re
import glob
import json
import shutil
import argparse
import xml.etree.ElementTree as ET

I18N_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "i18n"))
TMP_DIR = "/tmp/i18n_work"

def escape_xml(s):
    if not s:
        return ""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("'", "&apos;")

def unmark_existing_translations(content):
    def unmark(match):
        block = match.group(0)
        inner = match.group(1)
        if "<numerusform>" in inner:
            forms = re.findall(r'<numerusform>(.*?)</numerusform>', inner, re.DOTALL)
            if any(f.strip() for f in forms):
                return f"<translation>{inner}</translation>"
            return block
        if inner.strip():
            return f"<translation>{inner}</translation>"
        return block

    return re.sub(r'<translation type="unfinished">(.*?)</translation>', unmark, content, flags=re.DOTALL)

def get_ts_stats(file_path):
    tree = ET.parse(file_path)
    root = tree.getroot()
    lang = root.attrib.get("language", "")
    
    total = 0
    finished = 0
    empty_unf = 0
    text_unf = 0
    vanished = 0
    
    for msg in root.findall(".//message"):
        total += 1
        trans = msg.find("translation")
        if trans is None:
            continue
        ttype = trans.attrib.get("type")
        
        if msg.attrib.get("numerus") == "yes":
            forms = trans.findall("numerusform")
            text_content = "".join((f.text or "") for f in forms).strip()
        else:
            text_content = (trans.text or "").strip()
            
        if ttype == "unfinished":
            if not text_content:
                empty_unf += 1
            else:
                text_unf += 1
        elif ttype in ("vanished", "obsolete"):
            vanished += 1
        elif ttype is None:
            finished += 1
            
    pct = (finished / total * 100.0) if total > 0 else 0.0
    return {
        "file": os.path.basename(file_path),
        "path": file_path,
        "language": lang,
        "total": total,
        "finished": finished,
        "empty_unfinished": empty_unf,
        "text_unfinished": text_unf,
        "vanished": vanished,
        "pct_done": round(pct, 1)
    }

def cmd_rank(args):
    files = sorted(glob.glob(os.path.join(I18N_DIR, "*.ts")))
    stats_list = []
    for f in files:
        fname = os.path.basename(f)
        if fname == "drift.ts" and not args.include_base:
            continue
        stats_list.append(get_ts_stats(f))
        
    stats_list.sort(key=lambda s: (s["finished"], -s["empty_unfinished"]))
    
    if args.json:
        print(json.dumps(stats_list, indent=2))
        return
        
    print(f"{'Filename':18} | {'Lang':8} | {'Finished':8} | {'Empty Unf':10} | {'Text Unf':9} | {'% Done':6}")
    print("-" * 65)
    for s in stats_list:
        print(f"{s['file']:18} | {s['language']:8} | {s['finished']:8} | {s['empty_unfinished']:10} | {s['text_unfinished']:9} | {s['pct_done']:5}%")

def cmd_prepare(args):
    ts_file = args.file
    if not os.path.isabs(ts_file):
        ts_file = os.path.join(I18N_DIR, ts_file)
        
    fname = os.path.basename(ts_file)
    target_tmp_ts = os.path.join("/tmp", fname)
    work_dir = os.path.join(TMP_DIR, os.path.splitext(fname)[0])
    os.makedirs(work_dir, exist_ok=True)
    
    with open(ts_file, "r", encoding="utf-8") as f:
        content = f.read()
        
    marked_content = unmark_existing_translations(content)
    
    with open(target_tmp_ts, "w", encoding="utf-8") as f:
        f.write(marked_content)
        
    # Now parse target_tmp_ts to extract all empty unfinished messages
    tree = ET.parse(target_tmp_ts)
    root = tree.getroot()
    target_lang = root.attrib.get("language", "")
    
    items = []
    msg_id = 0
    for ctx in root.findall("context"):
        ctx_name_elem = ctx.find("name")
        ctx_name = ctx_name_elem.text if ctx_name_elem is not None else ""
        for msg in ctx.findall("message"):
            msg_id += 1
            trans = msg.find("translation")
            if trans is None:
                continue
            ttype = trans.attrib.get("type")
            is_numerus = (msg.attrib.get("numerus") == "yes")
            
            if is_numerus:
                forms = [f.text or "" for f in trans.findall("numerusform")]
                has_text = any(f.strip() for f in forms)
            else:
                has_text = bool((trans.text or "").strip())
                
            if ttype == "unfinished" and not has_text:
                src_elem = msg.find("source")
                source_text = src_elem.text if src_elem is not None else ""
                comment_elem = msg.find("comment")
                comment_text = comment_elem.text if comment_elem is not None else None
                
                item = {
                    "id": msg_id,
                    "context": ctx_name,
                    "source": source_text,
                    "is_numerus": is_numerus
                }
                if is_numerus:
                    item["num_forms"] = len(forms) if forms else 1
                if comment_text:
                    item["comment"] = comment_text
                items.append(item)
                
    # Chunk items
    chunk_size = args.chunk_size
    chunks = []
    for i in range(0, len(items), chunk_size):
        chunk_items = items[i:i + chunk_size]
        chunk_idx = (i // chunk_size) + 1
        chunk_filename = f"chunk_{chunk_idx:03d}.json"
        chunk_path = os.path.join(work_dir, chunk_filename)
        with open(chunk_path, "w", encoding="utf-8") as f:
            json.dump({
                "chunk_index": chunk_idx,
                "file": fname,
                "language": target_lang,
                "count": len(chunk_items),
                "items": chunk_items
            }, f, ensure_ascii=False, indent=2)
        chunks.append({
            "index": chunk_idx,
            "filename": chunk_filename,
            "count": len(chunk_items),
            "status": "pending"
        })
        
    manifest = {
        "file": fname,
        "language": target_lang,
        "source_path": ts_file,
        "tmp_ts_path": target_tmp_ts,
        "total_empty_to_translate": len(items),
        "chunk_size": chunk_size,
        "total_chunks": len(chunks),
        "chunks": chunks
    }
    manifest_path = os.path.join(work_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        
    print(f"Prepared {fname} in {work_dir}:")
    print(f"  Working copy: {target_tmp_ts}")
    print(f"  Empty strings to translate: {len(items)}")
    print(f"  Chunks created: {len(chunks)} (size: {chunk_size})")

def cmd_apply(args):
    target_ts = args.file
    if not os.path.exists(target_ts):
        if os.path.exists(os.path.join("/tmp", target_ts)):
            target_ts = os.path.join("/tmp", target_ts)
        elif os.path.exists(os.path.join(I18N_DIR, target_ts)):
            target_ts = os.path.join(I18N_DIR, target_ts)
            
    with open(args.translations, "r", encoding="utf-8") as f:
        data = json.load(f)
        
    trans_map = {}
    if isinstance(data, dict) and "items" in data:
        items = data["items"]
    elif isinstance(data, list):
        items = data
    elif isinstance(data, dict):
        items = []
        for k, v in data.items():
            try:
                kid = int(k)
                if isinstance(v, str):
                    items.append({"id": kid, "translation": v})
                elif isinstance(v, dict):
                    item_dict = dict(v)
                    item_dict["id"] = kid
                    items.append(item_dict)
            except ValueError:
                pass
    else:
        items = []
        
    for item in items:
        trans_map[item["id"]] = item
        
    with open(target_ts, "r", encoding="utf-8") as f:
        content = f.read()
        
    msg_id = 0
    updated_count = 0
    
    def replace_message(match):
        nonlocal msg_id, updated_count
        msg_id += 1
        block = match.group(0)
        
        if msg_id in trans_map:
            item = trans_map[msg_id]
            is_numerus = 'numerus="yes"' in block
            
            trans_match = re.search(r'(\n([ \t]*))<translation', block)
            indent = trans_match.group(2) if trans_match else "        "
            
            if is_numerus:
                forms = item.get("forms", [])
                if not forms and "translation" in item:
                    forms = [item["translation"]]
                sub_indent = indent + "    "
                inner_lines = []
                for form in forms:
                    inner_lines.append(f"{sub_indent}<numerusform>{escape_xml(form)}</numerusform>")
                inner_content = "\n" + "\n".join(inner_lines) + f"\n{indent}"
                new_trans = f"<translation>{inner_content}</translation>"
            else:
                new_text = escape_xml(item.get("translation", ""))
                new_trans = f"<translation>{new_text}</translation>"
                
            block = re.sub(r'<translation.*?>.*?</translation>', new_trans, block, flags=re.DOTALL)
            updated_count += 1
            
        return block
        
    new_content = re.sub(r'(<message.*?>.*?</message>)', replace_message, content, flags=re.DOTALL)
    
    dest_file = args.output if args.output else target_ts
    os.makedirs(os.path.dirname(os.path.abspath(dest_file)), exist_ok=True)
    with open(dest_file, "w", encoding="utf-8") as f:
        f.write(new_content)
        
    try:
        ET.parse(dest_file)
        print(f"Applied {updated_count} translations to {dest_file} (valid XML)")
    except Exception as e:
        print(f"ERROR: Resulting file {dest_file} is not valid XML: {e}", file=sys.stderr)
        sys.exit(1)

def cmd_finish(args):
    fname = os.path.basename(args.file)
    tmp_ts = os.path.join("/tmp", fname)
    final_ts = os.path.join(I18N_DIR, fname)
    
    if not os.path.exists(tmp_ts):
        print(f"ERROR: Temporary file {tmp_ts} not found!", file=sys.stderr)
        sys.exit(1)
        
    # Verify XML
    try:
        tree = ET.parse(tmp_ts)
    except Exception as e:
        print(f"ERROR: {tmp_ts} failed XML validation: {e}", file=sys.stderr)
        sys.exit(1)
        
    stats = get_ts_stats(tmp_ts)
    print(f"Validation stats for {fname}:")
    print(f"  Finished: {stats['finished']}")
    print(f"  Empty unfinished: {stats['empty_unfinished']}")
    print(f"  Text unfinished: {stats['text_unfinished']}")
    print(f"  Completion: {stats['pct_done']}%")
    
    if stats['empty_unfinished'] > 0 and not args.force:
        print(f"WARNING: File still has {stats['empty_unfinished']} empty unfinished strings. Use --force to copy anyway.", file=sys.stderr)
        sys.exit(1)
        
    shutil.copyfile(tmp_ts, final_ts)
    print(f"SUCCESS: Copied {tmp_ts} -> {final_ts}")

def cmd_mark_finished(args):
    if args.file:
        files = [args.file if os.path.isabs(args.file) else os.path.join(I18N_DIR, args.file)]
    else:
        files = sorted(glob.glob(os.path.join(I18N_DIR, "*.ts")))
        
    for fpath in files:
        fname = os.path.basename(fpath)
        if fname == "drift.ts":
            continue
        with open(fpath, "r", encoding="utf-8") as f:
            content = f.read()
        new_content = unmark_existing_translations(content)
        if new_content != content:
            ET.fromstring(new_content)
            with open(fpath, "w", encoding="utf-8") as f:
                f.write(new_content)
            stats = get_ts_stats(fpath)
            print(f"Updated {fname:18}: {stats['finished']:5} finished, {stats['empty_unfinished']:5} empty remaining ({stats['pct_done']}%)")
        else:
            print(f"No changes needed for {fname}")

def main():
    parser = argparse.ArgumentParser(description="i18n translation management tool")
    subparsers = parser.add_subparsers(dest="command")
    
    # rank
    rank_p = subparsers.add_parser("rank", help="Rank ts files by least translated")
    rank_p.add_argument("--json", action="store_true", help="Output JSON")
    rank_p.add_argument("--include-base", action="store_true", help="Include drift.ts base")
    
    # prepare
    prep_p = subparsers.add_parser("prepare", help="Prepare workspace and chunks in /tmp")
    prep_p.add_argument("file", help="TS file name or path")
    prep_p.add_argument("--chunk-size", type=int, default=100, help="Number of strings per chunk (default 100)")
    
    # apply
    app_p = subparsers.add_parser("apply", help="Apply translations to TS file")
    app_p.add_argument("file", help="Target TS file path (in /tmp)")
    app_p.add_argument("--translations", "-t", required=True, help="Translations JSON file")
    app_p.add_argument("--output", "-o", help="Optional output TS file path (defaults to in-place in /tmp)")
    
    # finish
    fin_p = subparsers.add_parser("finish", help="Validate and copy finished /tmp file to i18n/")
    fin_p.add_argument("file", help="TS file name (e.g. drift_bn.ts)")
    fin_p.add_argument("--force", action="store_true", help="Copy even if some unfinished remain")

    # mark-finished
    mf_p = subparsers.add_parser("mark-finished", help="Mark all non-empty unfinished translations as finished in-place")
    mf_p.add_argument("file", nargs="?", help="Optional TS file name (default: all files in i18n/)")
    
    args = parser.parse_args()
    if args.command == "rank":
        cmd_rank(args)
    elif args.command == "prepare":
        cmd_prepare(args)
    elif args.command == "apply":
        cmd_apply(args)
    elif args.command == "finish":
        cmd_finish(args)
    elif args.command == "mark-finished":
        cmd_mark_finished(args)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
