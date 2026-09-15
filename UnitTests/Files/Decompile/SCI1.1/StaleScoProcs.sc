;;; Sierra Script 1.0 - (do not remove this comment)
(script# 952)
(include sci.sh)

; Two public procs. The test drops export index 1 from the compiled .sco to
; simulate a stale .sco with fewer exports, then decompiles.
(public
	staleProcA 0
	staleProcB 1
)
(procedure (staleProcA param0)
	(return param0)
)
(procedure (staleProcB param0)
	(return param0)
)
