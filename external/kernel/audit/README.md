# Linux 7.1 PC-98 source-provenance audit

This directory records the provenance review of every substantive PC-98
implementation area in this tree.  It separates historical official Linux
PC-98 code, BSD-derived code, ordinary Linux 7.1 API integration, and new work
owned by the project.

The documents serve different purposes:

* `LINUX-7.1-PC98-PROVENANCE-AUDIT.md` is the current-tree conclusion and the
  file/hunk-level source ledger.
* `PC98-CODE-PROVENANCE-AUDIT.md` reconstructs the exact 1,000-line candidate
  set first introduced by the separately audited Linux 6.12 repository.
* `PC98-HUNK-PROVENANCE.md` describes the clean reconstruction hunk by hunk.
* `PC98-PORTING-REPORT.md` records implementation and validation work.
* `PC98-CLEAN-RECONSTRUCTION.md` records how the clean tree was produced.
* `linux-7.1-pc98.patch` is the mechanically generated single review patch.
* `PC98-CURRENT-HUNK-MANIFEST.tsv` lists every unified-diff hunk in that patch
  and assigns a conservative source class.
* `tools/scan-candidate-blocks.py` reproduces the exact normalized-block scan.
* `tools/generate-pc98-patch.py` reproduces the patch and hunk manifest from a
  vanilla Linux 7.1 tree.

The separately audited Linux 6.12 tree is an audit input only.  It is not an
implementation source.  The sole explicit exception is the keyboard code that
Awe Morris supplied to that repository; the retained BSD notices and project
copyright make that exception visible in the source.
