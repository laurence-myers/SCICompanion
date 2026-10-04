;;; Sierra Script 1.0 - (do not remove this comment)
(script# 980)
(include sci.sh)

; A term of an and whose first statement is a load that no instruction
; reads. As a group, its text would be (local0 (Abs param2)): a call of
; local0. The function falls back to asm.
(local
	local0
)

(public
	v6GroupValueFirst 0
)

(procedure (v6GroupValueFirst param1 param2 &tmp temp0)
	(asm
		lap param1
		bnt elseBranch
		lal local0
		push1
		lsp param2
		callk Abs, 2
		bnt elseBranch
		ldi 1
		sat temp0
		jmp done
	elseBranch:
		ldi 2
		sat temp0
	done:
		ret
	)
)
