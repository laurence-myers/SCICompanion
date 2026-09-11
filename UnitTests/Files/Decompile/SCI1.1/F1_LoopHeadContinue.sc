;;; Sierra Script 1.0 - (do not remove this comment)
(script# 900)
(include sci.sh)

; Family 1: a conditional branch to the loop head.
; The if has no else. It is the last statement in the loop body. The compiler
; sends the if false path to the loop head. The loop now has two back edges.
; The decompiler merges them into a common latch node. Then/else resolution
; then fails.
(public
	f1LoopHeadContinue 0
)

(procedure (f1LoopHeadContinue &tmp temp0 temp1)
	(asm
		ldi 0
		sat temp0
	loopHead:
		lat temp0
		ldi 10
		lt?
		bnt loopExit
		lat temp1
		bnt loopHead
		lat temp0
		ldi 1
		add
		sat temp0
		jmp loopHead
	loopExit:
		ret
	)
)
