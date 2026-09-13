;;; Sierra Script 1.0 - (do not remove this comment)
(script# 907)
(include sci.sh)

; Family 7: a class opcode names a species whose defining script is not in
; the game, so the class name lookup returns empty. The decompiler now
; synthesizes Unknown_Class_<species> and emits a classdef for it, so the
; function decompiles and round-trips without falling back to asm.
(public
	f7UnknownClass 0
	f7SecondClass 1
)

(procedure (f7UnknownClass)
	(asm
		pushi #new
		pushi 0
		class 999
		send 4
		ret
	)
)

(procedure (f7SecondClass)
	(asm
		pushi #new
		pushi 0
		class 998
		send 4
		ret
	)
)
