;;; Sierra Script 1.0 - (do not remove this comment)
(script# 919)
(include sci.sh)

; Family 8: a dead value computed before the test of an if that a "ret"
; consumes. The dead value lifts out as a bare statement.
; Sierra: (* param2 8) (return (if param1 1 else 0)).
(public
	f8DeadValueStatement 0
)

(procedure (f8DeadValueStatement param1 param2)
	(asm
		lsp param2
		ldi 8
		mul
		lap param1
		bnt elseBranch
		ldi 1
		jmp join
	elseBranch:
		ldi 0
	join:
		ret
	)
)
