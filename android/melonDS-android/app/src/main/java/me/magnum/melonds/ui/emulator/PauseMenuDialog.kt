package me.magnum.melonds.ui.emulator

import android.animation.AnimatorInflater
import android.content.Context
import android.content.res.ColorStateList
import android.os.Bundle
import android.view.Gravity
import android.widget.GridLayout
import androidx.appcompat.app.AppCompatDialog
import androidx.appcompat.widget.AppCompatButton
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import me.magnum.melonds.R
import me.magnum.melonds.databinding.DialogPauseMenuBinding

/** A controller-friendly pause menu that retains the existing option dispatch and cancel behavior. */
class PauseMenuDialog(
    context: Context,
    private val options: List<PauseMenuOption>,
    private val onOptionSelected: (PauseMenuOption) -> Unit,
) : AppCompatDialog(context, R.style.PauseMenuTheme) {

    private fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val binding = DialogPauseMenuBinding.inflate(layoutInflater)
        setContentView(binding.root)
        setCanceledOnTouchOutside(true)

        val metrics = context.resources.displayMetrics
        val width = minOf(metrics.widthPixels - dp(32), dp(720))
        val columns = if (width >= dp(500)) 2 else 1
        binding.pauseActions.columnCount = columns

        styleButton(binding.resumeGame, R.drawable.ic_input)
        binding.resumeGame.setTextColor(ContextCompat.getColor(context, R.color.settings_accent))
        // Cancel is deliberately used here: it follows the same resume path as Back/outside-tap.
        binding.resumeGame.setOnClickListener { cancel() }

        options.forEachIndexed { index, option ->
            val button = AppCompatButton(context).apply {
                id = android.view.View.generateViewId()
                setText(option.textResource)
                styleButton(this, iconFor(option))
                setOnClickListener {
                    // Match AlertDialog's original ordering: dispatch the action, then dismiss.
                    onOptionSelected(option)
                    dismiss()
                }
            }
            binding.pauseActions.addView(button, GridLayout.LayoutParams(
                GridLayout.spec(index / columns),
                GridLayout.spec(index % columns, 1f),
            ).apply {
                this.width = 0
                height = dp(58)
                setMargins(dp(3), dp(4), dp(3), dp(4))
            })
        }

        val rows = (options.size + columns - 1) / columns
        val height = minOf(metrics.heightPixels - dp(48), dp(176 + rows * 66))
        window?.setLayout(width, height)
        binding.resumeGame.requestFocus()
    }

    private fun styleButton(button: AppCompatButton, icon: Int) {
        button.apply {
            isAllCaps = false
            textSize = 17f
            gravity = Gravity.CENTER_VERTICAL or Gravity.START
            setPaddingRelative(dp(16), 0, dp(16), 0)
            setTextColor(ContextCompat.getColor(context, R.color.settings_text))
            supportBackgroundTintList = null
            setBackgroundResource(R.drawable.settings_card)
            elevation = dp(3).toFloat()
            stateListAnimator = AnimatorInflater.loadStateListAnimator(context, R.animator.settings_card_lift)
            val drawable = ContextCompat.getDrawable(context, icon)?.mutate()?.apply {
                DrawableCompat.setTintList(this, ColorStateList.valueOf(ContextCompat.getColor(context, R.color.settings_accent)))
                setBounds(0, 0, dp(24), dp(24))
            }
            setCompoundDrawablesRelative(drawable, null, null, null)
            compoundDrawablePadding = dp(12)
        }
    }

    private fun iconFor(option: PauseMenuOption) = when (option.textResource) {
        R.string.settings -> R.drawable.ic_settings
        R.string.save_state -> R.drawable.ic_file
        R.string.load_state -> R.drawable.ic_folder
        R.string.rewind -> R.drawable.ic_clock
        R.string.cheats -> R.drawable.ic_cheat
        R.string.achievements -> R.drawable.ic_trophy
        R.string.reset -> R.drawable.ic_refresh
        R.string.exit -> R.drawable.ic_clear
        else -> R.drawable.ic_menu
    }
}
