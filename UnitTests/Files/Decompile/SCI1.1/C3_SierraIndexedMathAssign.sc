;;; Sierra Script 1.0 - (do not remove this comment)
(script# 932)
(include sci.sh)

; Sierra's sc compiles (+= [temp0 param1] param2) as "index; lati; push;
; value; add; push; index; sati": a load to the accumulator and a push, where
; SCI Companion emits "lsti". Both fold back to the compound assignment.
; Sierra: (+= [temp0 param1] param2) (-= [temp0 (+ param1 1)] 1)
(public
	c3SierraIndexedMathAssign 0
)

(procedure (c3SierraIndexedMathAssign param1 param2 &tmp [temp0 4])
	(asm
		lap param1
		lati temp0
		push
		lap param2
		add
		push
		lap param1
		sati temp0
		lsp param1
		ldi 1
		add
		lati temp0
		push
		ldi 1
		sub
		push
		lsp param1
		ldi 1
		add
		sati temp0
		ret
	)
)
