import { HTMLAttributes } from "react";
import { clsx } from "clsx";

export function Card({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={clsx(
        "rounded-2xl bg-slate-900/60 p-4 shadow-lg ring-1 ring-slate-800 backdrop-blur",
        className
      )}
      {...props}
    />
  );
}
