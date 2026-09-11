;;; Sierra Script 1.0 - (do not remove this comment)
(script# 907)
(include sci.sh)

; Family 7: a class opcode names a species that is not in the class table.
; The class name lookup returns empty. Instruction consumption then throws
; "Unexpected opcode" for the class opcode.
(public
	f7UnknownClass 0
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
