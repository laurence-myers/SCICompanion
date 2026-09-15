;;; Sierra Script 1.0 - (do not remove this comment)
(script# 940)
(include sci.sh)

; A public procedure whose real name starts with "proc" but is not one of the
; decompiler's generated procN_i names. Its name is written into the .sco on
; compile; on decompile _IsUndeterminedPublicProc runs stoi on the "Foo" tail.
(public
	procFoo 0
)
(procedure (procFoo param0)
	(return param0)
)
