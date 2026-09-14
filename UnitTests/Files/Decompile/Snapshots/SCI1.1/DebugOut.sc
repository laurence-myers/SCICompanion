;;; Sierra Script 1.0 - (do not remove this comment)
(script# 14)
(include sci.sh)
(use DebugRoomInit)
(use Print)
(use File)
(use Game)

(public
	ndebug_log 0
)

(instance ndebug_log of File
	(properties
		name "ndebug.log"
	)
	
	(method (init param1 &tmp temp0 [temp1 10] temp11 temp12 temp13)
		(= temp11 -1)
		(= temp0 (FileIO 0 param1 1))
		(if temp0
			(if (!= (FileIO 5 @temp1 10 temp0) -1)
				(= temp12 (ReadNumber @temp1))
				(if temp12
					(= temp13 (ScriptID temp12 0))
					(if (IsObject temp13)
						(if (temp13 isKindOf: Rm)
							(= temp11 temp12)
							(DebugRoomInit temp11)
							(DisposeScript 16)
						)
						(DisposeScript temp12)
					)
				)
			)
			(FileIO 1 temp0)
		)
		(self open: 0)
		(return temp11)
	)
	
	(method (debugPrint &tmp temp0 temp1 temp2)
		(= temp0 (FileIO 0 {debug.log} 0))
		(if temp0
			(= temp1 (FindFormatLen &rest))
			(= temp2 (Memory 1 temp1))
			(Format temp2 &rest)
			(FileIO 6 temp0 temp2)
			(FileIO 6 temp0 {\n})
			(Memory 3 temp2)
			(FileIO 1 temp0)
		)
	)
)
