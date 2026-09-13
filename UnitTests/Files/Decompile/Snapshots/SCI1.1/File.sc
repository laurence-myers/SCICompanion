;;; Sierra Script 1.0 - (do not remove this comment)
(script# 993)
(include sci.sh)
(use System)


(class File of Obj
	(properties
		handle 0
	)
	
	(method (dispose)
		(self close:)
		(super dispose:)
	)
	
	(method (showStr param1)
		(Format param1 {File: %s} name)
	)
	
	(method (open param1)
		(= handle
			(switch argc
				(0 (FileIO 0 name 0))
				(1 (FileIO 0 name param1))
				(else  0)
			)
		)
		(if (== handle -1) (= handle 0))
		(return (if handle self else 0))
	)
	
	(method (readString param1 param2)
		(if (!= argc 2) (return 0))
		(if (not handle) (self open: 1))
		(return (if handle (FileIO 5 param1 param2 handle) else 0))
	)
	
	(method (writeString param1 &tmp temp0)
		(if (not handle) (self open:))
		(if handle
			(= temp0 0)
			(while (< temp0 argc)
				(if (not (FileIO 6 handle [param1 temp0])) (return 0))
				(++ temp0)
			)
		)
		(return 1)
	)
	
	(method (write param1 param2 &tmp temp0)
		(if (not handle) (self open:))
		(return (if handle (FileIO 3 handle param1 param2) else 0))
	)
	
	(method (read param1 param2)
		(if (!= argc 2) (return 0))
		(if (not handle) (self open: 1))
		(return (if handle (FileIO 2 handle param1 param2) else 0))
	)
	
	(method (seek param1 param2 &tmp temp0)
		(= temp0 (if (>= argc 2) param2 else 0))
		(if (not handle) (self open: 1))
		(return (if handle (FileIO 7 handle param1 temp0) else 0))
	)
	
	(method (close)
		(if handle (FileIO 1 handle) (= handle 0))
	)
	
	(method (delete)
		(if handle (self close:))
		(FileIO 4 name)
	)
)
