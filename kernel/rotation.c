#include<linux/slab.h>
#include<linux/rotation.h>
//#include<asm/uaccess.h>
#include<linux/printk.h>
#include<linux/spinlock.h>
#include<linux/list.h>
#include<linux/sched.h>
#include<asm-generic/current.h>
#include<linux/wait.h>

/*
 * set_rotation() simply copy the degree from user space to kernel space.
 * global_rotation is declared in rotation.h
 * after copying the degree, it calls common function to let other processes * know the new rotation and be rescheduled.
 *
 */


/* initialize global variables */

int global_rotation = 0;

LIST_HEAD(waiting_list_head);
LIST_HEAD(acquiring_list_head);

DEFINE_SPINLOCK(global_lock);

asmlinkage long sys_set_rotation(int degree)
{

	spin_lock(&global_lock);

	global_rotation = degree;
	
	printk("Current degree is %d\n",global_rotation);

//	rescheduler();

	spin_unlock(&global_lock);
	
	return 0;
	// !!should modify return value
}


/*
 * Take a read/or write lock using the given rotation range
 * returning 0 on success, -1 on failure.
 * system call numbers 381 and 382
 */
asmlinkage long sys_rotlock_read(int degree, int range)	/* 0 <= degree < 360 , 0 < range < 180 */
{	
	int should_I_sleep = 0;
	struct proc_lock_info * new_proc = kmalloc(sizeof(struct proc_lock_info), GFP_KERNEL); // !!나중에 리스트에서없엘때 꼭 프리해줄것
	new_proc->degree = degree;
	new_proc->range = range;
	new_proc->type = READ;
	new_proc->task = current;
	INIT_LIST_HEAD(&(new_proc->sibling));


	spin_lock(&global_lock);
	
	if(is_in_range(degree, range, global_rotation)) // 현재 각도를 보고 락 범위안에 포함되는지 판단
	{
		if(check_acquiring_list(degree,range,new_proc)) // 현재 ac_list를 보고 이 프로세스가 lock을 얻을 수 있는지 없는지 판단
		{
			// 락을 얻을 수 있으면 ac_list 에 추가, wait 안하고 탈출
			
			list_add_tail(&(new_proc->sibling), &acquiring_list_head);
			
			should_I_sleep = 0;

		}
		else
		{
			// 얻을 수 없으면 wait_list 에 추가, wait 시켜서 못빠져나가게.. !! spin_unlock(&global_lock) 밖에서 재워야함?
			
			list_add_tail(&(new_proc->sibling), &waiting_list_head);
			
			should_I_sleep = 1;
		}
	}
	else // 현재 각도가 락 범위 밖이면 더 볼것없이 wait
	{
		// wait_list에 추가
		list_add_tail(&(new_proc->sibling), &waiting_list_head);
		
		should_I_sleep = 1;
	}

	spin_unlock(&global_lock);

	if(should_I_sleep)
	{

		printk("I am sleeping now zZZ\n");
		
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();

		/*
		remove_wait_queue
		wake_up_process
		sleep_on
		wake_up
		wait_queue_t
		wait_queue_head_t
		wait_event();*/ 
	}
	printk("Good moring\n");

	return 0;
}

/*
 * acquiring_list 를 확인하고 lock 을 잡을수 있으면 return 1, 아니면 0
 *
 */

int check_acquiring_list(int degree, int range, struct proc_lock_info *new_proc)
{
	struct proc_lock_info * cursor;
	//if(list_empty(&acquiring_list_head))  // acquiring_list 가 비어있다면 락을 잡을수 있다.
	//	return 1;

	//else // 비어있지 않다면
	//{
		list_for_each_entry(cursor, &acquiring_list_head, sibling) // 리스트를 돌면서
		{
			if(is_overwrapped(cursor->degree,cursor->range, degree,range)) // 자기 범위와 겹치는 애가 있는지 확인하고
			{
				if(is_writer(cursor)) // 겹치는 애가 writer 라면 락을 잡을수 없고
					return 0;
				else
				{
					if(is_writer(new_proc)) // 겹치는 애가 reader라도 자기가 writer면 lock을 잡을 수 없다.
						return 0;
				}
			}
		}
	//}
	return 1; // 겹치는 애가 하나도 없거나 reader끼리만 겹칠경우는 lock을 잡을 수 있다.
}



/*
 * proc_lock_info 가 writer면 1리턴, 아니면 0리턴
 */

int is_writer(struct proc_lock_info * pli)
{
	if(pli->type == WRITE)
		return 1;
	else
		return 0;
}

/*
 * 두 쌍의 degree, range 범위가 겹치면 리턴 1, 아니면 리턴 0
 */

int is_overwrapped(int d1, int r1, int d2, int r2)
{

	int upper1 = (d1+r1)%360; 
	int lower1 = (d1-r1+360)%360;
	int upper2 = (d2+r2)%360;
	int lower2 = (d2-r2+360)%360;

	

	if(lower1 > upper1) // ex) [330,30]
	{
		if(lower2 > upper2)
			return 1;
		else
		{
			if((lower1 <= upper2 ) || (upper1 >= lower2))
				return 1;
		}

	}
	else if(lower2 > upper2)
	{
		if((lower2 <= upper1 ) || (upper2 >= lower1))
			return 1;
	}

	else if((upper1 >= lower2) && (lower1 <= upper2 ))
	{
		return 1;
	}

	return 0;

}

/*
 * rotation 값이 범위안에 들어가는지 판단, 들어가면 1, 아니면 0
 */

int is_in_range(int degree, int range, int rotation)
{
	int upper = (degree+range)%360; 
	int lower = (degree-range)%360;

	if(upper < lower)
	{
		if(rotation >= lower || rotation <= upper)
			return 1;
	}
	else
	{
		if(rotation >= lower && rotation <= upper)
			return 1;
	}

	return 0;
	
}

//int rotlock_write(int degree, int range);	/* degree - range <= LOCK RANGE <= degree + range */

/*
 * common function that manages all shared variables.
 * this function should 
 * 1. update all shared variables we use.----------- X !!it will be done in caller function
 * 2. reschedule processes that are waiting or running. (Maybe using signal)
 *
 */

/*
void rescheduler()
{

	1. updating part ------------ X !!it will be done in caller function

	2. rescheduling part
	struct proc_lock_info * cursor;
	int lower = 0, upper = 0;
	macro(cursor : waiting_list) // traverse waiting list
	{
		cursor->degree - cursor->range = lower;
		cursor->degree + cursor->range = upper;
		if(global_rotation < upper && global_rotation > lower) // check if the global_rotation fits in range.
		{
			if(check_acquiring_list(cursor->degree,cursor->range) <= 0) // check if there already exist acquiring lock in range
			{
				wake_up(cursor);
			}
		}
	}	
	
	!! 각 리스트에 있는 애들중에 갑자기 죽은애들이 있는지도 매번 확인해줘야함

}

*/

asmlinkage long sys_rotlock_write(int degree, int range) 
{
		printk("sys_rotlock_write(382) called\n");
			return 0;
}

asmlinkage long sys_rotunlock_read(int degree, int range) 
{
	wake_up_process(temp);
			return 0;
}

asmlinkage long sys_rotunlock_write(int degree, int range) 
{
		printk("sys_rotunlock_write(385) called\n");
			return 0;
}
